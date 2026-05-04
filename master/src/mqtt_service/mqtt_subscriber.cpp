/**
 * main.cpp — mqtt_subscriber 进程
 *
 * 职责：
 *   订阅 broker 上的 car/+/event，解析从机上报的事件，
 *   更新 SHM 中的小车状态，并根据事件类型向 mq_task_dispatch
 *   或 mq_mqtt_publish 发布后续动作。
 *
 * 数据流：
 *   从机小车
 *       → broker
 *       → car/<id>/event (MQTT)
 *       → [本进程 on_message]
 *       → SHM CarData（更新位置/状态）
 *       → mq_task_dispatch（触发重规划）或 mq_mqtt_publish（发下一条命令）
 *
 * 调试模式（本文件当前状态）：
 *   不写 SHM（SHM 可能未启动），只打印收到的消息并原样回显确认，
 *   便于单独测试 MQTT 链路。
 *
 * 事件 topic 格式：
 *   car/<car_id>/event
 *   payload JSON 示例：
 *     {"type":"reached","node":3}
 *     {"type":"obstacle","kind":"temporary"}
 *
 * 命令行：
 *   mqtt_subscriber <broker_host> [broker_port]
 *
 * 编译：
 *   g++ -std=c++17 \
 *       -I../../ -I../../lib \
 *       -o mqtt_subscriber main.cpp \
 *       -lmosquitto -lrt -lpthread
 */

#define AGV_NO_SYSTEMD

#include "mqtt_client.h"
#include "signal_handler.h"
#include "secure_exit.h"
#include "mq_wrapper.h"
#include "config/config.h"
#include "logger.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <poll.h>
#include <string>

static const char* PROC_NAME = "mqtt_subscriber";

// ─────────────────────────────────────────────────────────────────────────────
// 事件解析（不依赖 JSON 库，手动解析键值）
// ─────────────────────────────────────────────────────────────────────────────

enum class EventType : uint8_t {
    kARRIVE  = 0,
    kOBSTACLE  = 1,
    kREPAIRED = 2,
    kACK=3,
    kINFO=4,
};

struct CarEvent {
    EventType type;
    uint8_t   car_id;    // 从 topic 中解析

    int16_t   val_param;
    std::string param;
    bool is_temporary;  // 仅针对障碍事件，表示是否为临时障碍
};

/**
 * 从 topic "car/<id>/event" 中解析 car_id。
 * 返回 0xFF 表示解析失败。
 */
static uint8_t parse_car_id(const char* topic) {
    // topic 格式：car/N/event
    const char* p = topic + 4;   // 跳过 "car/"
    char* end = nullptr;
    long id = strtol(p, &end, 10);
    if (end == p || id < 0 || id > 254) return 0xFF;
    return static_cast<uint8_t>(id);
}

/**
 * 极简 JSON 字段提取：找 "key":"value" 或 "key":number。
 * 不做完整 JSON 解析，只处理已知的扁平格式。
 * 返回指向值起始的指针，len 为值长度（不含引号）。
 */
static const char* json_get(const char* json, const char* key,
                             int* len_out) {
    // 在 json 中找 "key":
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\":", key);
    const char* p = strstr(json, needle);
    if (!p) return nullptr;
    p += strlen(needle);
    while (*p == ' ') ++p;   // 跳过空格

    if (*p == '"') {
        // 字符串值
        ++p;
        const char* end = strchr(p, '"');
        if (!end) return nullptr;
        *len_out = static_cast<int>(end - p);
        return p;
    } else {
        // 数值
        const char* start = p;
        while (*p >= '0' && *p <= '9') ++p;
        *len_out = static_cast<int>(p - start);
        return start;
    }
}


bool is_temp(const char* input) {
    static const char* list[] = { AGC_TMP_OBSTACLE };
    for (int i = 0; list[i] != nullptr; ++i) {
        if (strcmp(input, list[i]) == 0) return true;
    }
    return false;
}

static CarEvent parse_event(const char* topic, const char* payload) {
    CarEvent ev{};
    ev.car_id = parse_car_id(topic);

    int vlen = 0;
    const char* type_val = json_get(payload, "type", &vlen);
    if (!type_val){
        LOG_ERROR(PROC_NAME,"failed to analyse json");
        return ev;
    } 

    if (strncmp(type_val, "ARRIVE", vlen) == 0) {
        ev.type = EventType::kARRIVE;
        //const char* node_val = json_get(payload, "node", &vlen);
    } else if (strncmp(type_val, "OBSTACLE", vlen) == 0) {
        ev.type = EventType::kOBSTACLE;
        const char* kind_val = json_get(payload, "param", &vlen);
        if (kind_val && vlen < AGC_MAX_LABEL-1) {
            char buf[AGC_MAX_LABEL];
            memcpy(buf, kind_val, vlen);
            buf[vlen] = '\0'; 
            ev.param = std::string(buf);
            ev.is_temporary = is_temp(buf);
        }else{
            LOG_ERROR(PROC_NAME,"failed to analyse json");
        }
    } else if (strncmp(type_val, "REPAIRED", vlen) == 0) {
        ev.type = EventType::kREPAIRED;
    } else if (strncmp(type_val, "ACK", vlen) == 0) {
        ev.type = EventType::kACK;
    } else if (strncmp(type_val, "INFO", vlen) == 0) {
        ev.type = EventType::kINFO;
    } else {
        LOG_ERROR(PROC_NAME,"unknown event type in json");
    }
    //涉及到数字的操作
    //if (node_val) {
    //    ev.node_id = static_cast<uint8_t>(strtol(node_val, nullptr, 10));
    //}

    return ev;
}

// Subscriber MQTT 客户端


class SubscriberClient : public agv::MqttClientBase {
public:
    // 注入 MQ 发送端（init 后、connect 前调用）
    void set_mq_task(agv::MqSender<agv::TaskDispatchMsg>* mq) {
        mq_task_ = mq;
    }
    void set_mq_publish(agv::MqSender<agv::MqttPublishMsg>* mq) {
        mq_pub_ = mq;
    }

protected:
    void on_connect(int rc) override {
        if (rc != 0) return;

        // 订阅所有小车的事件 topic
        int ret = mosquitto_subscribe(mosq(), nullptr, "car/+/event", /*qos=*/1);
        if (ret == MOSQ_ERR_SUCCESS) {
            LOG_INFO(PROC_NAME, "subscribed to car/+/event");
        } else {
            LOG_ERROR(PROC_NAME, "subscribe failed: %s\n", mosquitto_strerror(ret));
        }
    }

    void on_message(const mosquitto_message* msg) override {
        if (!msg->payload || msg->payloadlen == 0) return;

        // 复制 payload 并确保 \0 结尾
        char payload[512] = {};
        int  plen = msg->payloadlen < (int)sizeof(payload) - 1
                    ? msg->payloadlen : (int)sizeof(payload) - 1;
        memcpy(payload, msg->payload, plen);
        LOG_INFO(PROC_NAME,"received topic=%s payload=%s", msg->topic, payload);

        CarEvent ev = parse_event(msg->topic, payload);
        if (ev.car_id == 0xFF) {
            LOG_ERROR(PROC_NAME, "parse error: bad car_id in topic");
            return;
        }
        dispatch(ev, payload);
    }

private:
    //TODO---根据小车情况采取行动
    //_Static_assert(0, "This macro is not allowed");

    void dispatch(const CarEvent& ev, const char* raw_payload) {
        switch (ev.type) {
            case EventType::kARRIVE:{
                LOG_INFO(PROC_NAME,"car%u arrived at node%u", ev.car_id, ev.val_param);
                break;
            }
             case EventType::kREPAIRED:{
                LOG_INFO(PROC_NAME,"car%u repaired obstacle", ev.car_id);
                break;
            }
             case EventType::kACK:{
                LOG_INFO(PROC_NAME,"car%u ack cmd", ev.car_id);
                break;
            }
             case EventType::kINFO:{
                LOG_INFO(PROC_NAME,"car%u info: %s", ev.car_id, ev.param.c_str());
                break;
            }
            default:
                LOG_ERROR(PROC_NAME, "unknown event type, payload=%s", raw_payload);
                break;
            }
    }
    agv::MqSender<agv::TaskDispatchMsg>* mq_task_ {nullptr};
    agv::MqSender<agv::MqttPublishMsg>*  mq_pub_  {nullptr};
};


int main() {
    const char* host = "localhost";
    int         port = 1883;
    LOG_INFO(PROC_NAME,"starting, broker=%s:%d", host, port);

    // ── 1. 信号处理 ───────────────────────────────────────────────
    agv::SignalHandler sig(PROC_NAME);
    try { sig.init(); }
    catch (const std::exception& e) {
        LOG_FATAL(PROC_NAME,"signal handler init failed: %s",e.what());
        return 1;
    }

    // MQ 发送端（可选，若 MQ 未启动则跳过）
    agv::MqSender<agv::TaskDispatchMsg> mq_task;
    agv::MqSender<agv::MqttPublishMsg>  mq_pub;
    bool mq_available = false;

    try {
        mq_task.init(agv::kMqTaskDispatch, agv::kTaskDispatchMsgSize);
        mq_pub .init(agv::kMqMqttPublish,  agv::kMqttPublishMsgSize);
        mq_available = true;
        LOG_INFO(PROC_NAME,"MQ connected");
    } catch (const std::exception& e) {
        LOG_ERROR(PROC_NAME,"MQ not available (%s), running in MQTT-only mode", e.what());
    }

    // MQTT 客户端
    SubscriberClient mqtt;
    if (mq_available) {
        mqtt.set_mq_task(&mq_task);
        mqtt.set_mq_publish(&mq_pub);
    }

    try {
        mqtt.init("agv-subscriber");
        mqtt.connect(host, port);
    } catch (const std::exception& e) {
        LOG_FATAL(PROC_NAME,"mqtt init failed: %s", e.what());
        return 1;
    }

    // 退出清理
    agv::SecureExit exit_seq(PROC_NAME);
    if (mq_available) {
        exit_seq.add_cleanup("mq_close", [&] {
            mq_task.close();
            mq_pub.close();
        });
    }

    //poll 只监听信号，MQTT 由后台线程处理)
    enum { FD_SIG = 0, FD_COUNT };
    struct pollfd fds[FD_COUNT];
    fds[FD_SIG].fd = sig.get_fd(); fds[FD_SIG].events = POLLIN;

    LOG_INFO(PROC_NAME,"ready, listening for car/+/event");

    while (!sig.shutdown_requested()) {
        int ret = ::poll(fds, FD_COUNT, -1);
        if (ret < 0) {
            if (errno == EINTR) continue;
            LOG_ERROR(PROC_NAME,"poll error: %s", strerror(errno));
            break;
        }

        if (fds[FD_SIG].revents & POLLIN) {
            sig.handle_read();
        }
    }

    LOG_INFO(PROC_NAME,"shutting down");
    exit_seq.run(100);
}
