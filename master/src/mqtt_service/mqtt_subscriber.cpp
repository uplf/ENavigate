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

#include "../../lib/mqtt/mqtt_client.h"
#include "../../lib/ipc/signal_handler.h"
#include "../../lib/ipc/graceful_exit.h"
#include "../../lib/ipc/mq_wrapper.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <poll.h>

static const char* PROC_NAME = "mqtt_subscriber";

// ─────────────────────────────────────────────────────────────────────────────
// 事件解析（不依赖 JSON 库，手动解析键值）
// ─────────────────────────────────────────────────────────────────────────────

enum class EventType : uint8_t {
    kUnknown  = 0,
    kReached  = 1,   // 到达节点
    kObstacle = 2,   // 障碍物
};

struct CarEvent {
    EventType type;
    uint8_t   car_id;    // 从 topic 中解析

    // kReached
    uint8_t   node_id;

    // kObstacle
    bool      is_temporary;   // true=临时, false=永久
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

static CarEvent parse_event(const char* topic, const char* payload) {
    CarEvent ev{};
    ev.car_id = parse_car_id(topic);

    int vlen = 0;
    const char* type_val = json_get(payload, "type", &vlen);
    if (!type_val) return ev;

    if (strncmp(type_val, "reached", vlen) == 0) {
        ev.type = EventType::kReached;
        const char* node_val = json_get(payload, "node", &vlen);
        if (node_val) {
            ev.node_id = static_cast<uint8_t>(strtol(node_val, nullptr, 10));
        }
    } else if (strncmp(type_val, "obstacle", vlen) == 0) {
        ev.type = EventType::kObstacle;
        const char* kind_val = json_get(payload, "kind", &vlen);
        ev.is_temporary = (kind_val && strncmp(kind_val, "temporary", vlen) == 0);
    }

    return ev;
}

// ─────────────────────────────────────────────────────────────────────────────
// Subscriber MQTT 客户端
// ─────────────────────────────────────────────────────────────────────────────

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
            fprintf(stderr, "[%s] subscribed to car/+/event\n", PROC_NAME);
        } else {
            fprintf(stderr, "[%s] subscribe failed: %s\n",
                    PROC_NAME, mosquitto_strerror(ret));
        }
    }

    void on_message(const mosquitto_message* msg) override {
        if (!msg->payload || msg->payloadlen == 0) return;

        // 复制 payload 并确保 \0 结尾
        char payload[512] = {};
        int  plen = msg->payloadlen < (int)sizeof(payload) - 1
                    ? msg->payloadlen : (int)sizeof(payload) - 1;
        memcpy(payload, msg->payload, plen);

        fprintf(stderr, "[%s] ← topic=%-24s  payload=%s\n",
                PROC_NAME, msg->topic, payload);

        CarEvent ev = parse_event(msg->topic, payload);
        if (ev.car_id == 0xFF) {
            fprintf(stderr, "[%s]   parse error: bad car_id in topic\n",
                    PROC_NAME);
            return;
        }

        dispatch(ev, payload);
    }

private:
    void dispatch(const CarEvent& ev, const char* raw_payload) {
        switch (ev.type) {

        case EventType::kReached:
            fprintf(stderr,
                "[%s]   → kReached: car=%u arrived at node=%u\n",
                PROC_NAME, ev.car_id, ev.node_id);

            // 调试模式：不写 SHM，只打印
            // 正式模式：agv::shm_set_car_status(shm, car_idx, CarStatus::IDLE, ev.node_id);

            // 触发重规划（若还有路径）
            // 调试模式：只打印，不真正发 MQ
            fprintf(stderr,
                "[%s]   → would send TaskDispatch::replan for car=%u\n",
                PROC_NAME, ev.car_id);

            if (mq_task_) {
                auto task = agv::TaskDispatchMsg::replan(ev.car_id);
                mq_task_->send(task, agv::kPrioNormal);
                fprintf(stderr, "[%s]   → sent replan to mq_task_dispatch\n",
                        PROC_NAME);
            }
            break;

        case EventType::kObstacle:
            fprintf(stderr,
                "[%s]   → kObstacle: car=%u kind=%s\n",
                PROC_NAME, ev.car_id,
                ev.is_temporary ? "temporary" : "permanent");

            // 临时障碍：停车等待
            if (ev.is_temporary && mq_pub_) {
                auto stop = agv::MqttPublishMsg::make_stop(ev.car_id, /*reason=*/1);
                mq_pub_->send(stop, agv::kPrioHigh);
                fprintf(stderr, "[%s]   → sent stop(emergency) to mq_mqtt_publish\n",
                        PROC_NAME);
            }
            break;

        default:
            fprintf(stderr, "[%s]   → unknown event type, payload=%s\n",
                    PROC_NAME, raw_payload);
            break;
        }
    }

    agv::MqSender<agv::TaskDispatchMsg>* mq_task_ {nullptr};
    agv::MqSender<agv::MqttPublishMsg>*  mq_pub_  {nullptr};
};

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <broker_host> [port]\n", argv[0]);
        return 1;
    }
    const char* host = argv[1];
    int         port = (argc >= 3) ? std::atoi(argv[2]) : 1883;

    fprintf(stderr, "[%s] starting, broker=%s:%d\n", PROC_NAME, host, port);

    // ── 1. 信号处理 ───────────────────────────────────────────────
    agv::SignalHandler sig(PROC_NAME);
    try { sig.init(); }
    catch (const std::exception& e) {
        fprintf(stderr, "[%s] FATAL signal: %s\n", PROC_NAME, e.what());
        return 1;
    }

    // ── 2. MQ 发送端（可选，若 MQ 未启动则跳过）──────────────────
    agv::MqSender<agv::TaskDispatchMsg> mq_task;
    agv::MqSender<agv::MqttPublishMsg>  mq_pub;
    bool mq_available = false;

    try {
        mq_task.init(agv::kMqTaskDispatch, agv::kTaskDispatchMsgSize);
        mq_pub .init(agv::kMqMqttPublish,  agv::kMqttPublishMsgSize);
        mq_available = true;
        fprintf(stderr, "[%s] MQ connected\n", PROC_NAME);
    } catch (const std::exception& e) {
        fprintf(stderr, "[%s] MQ not available (%s), running in MQTT-only mode\n",
                PROC_NAME, e.what());
    }

    // ── 3. MQTT 客户端 ────────────────────────────────────────────
    SubscriberClient mqtt;
    if (mq_available) {
        mqtt.set_mq_task(&mq_task);
        mqtt.set_mq_publish(&mq_pub);
    }

    try {
        mqtt.init("agv-subscriber");
        mqtt.connect(host, port);
    } catch (const std::exception& e) {
        fprintf(stderr, "[%s] FATAL mqtt: %s\n", PROC_NAME, e.what());
        return 1;
    }

    // ── 4. 退出清理 ───────────────────────────────────────────────
    agv::GracefulExit exit_seq(PROC_NAME);
    if (mq_available) {
        exit_seq.add_cleanup("mq_close", [&] {
            mq_task.close();
            mq_pub.close();
        });
    }

    // ── 5. poll 主循环（只监听信号，MQTT 由后台线程处理）─────────
    enum { FD_SIG = 0, FD_COUNT };
    struct pollfd fds[FD_COUNT];
    fds[FD_SIG].fd = sig.get_fd(); fds[FD_SIG].events = POLLIN;

    fprintf(stderr, "[%s] ready, listening for car/+/event\n", PROC_NAME);

    while (!sig.shutdown_requested()) {
        int ret = ::poll(fds, FD_COUNT, -1);
        if (ret < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "[%s] poll error: %s\n", PROC_NAME, strerror(errno));
            break;
        }

        if (fds[FD_SIG].revents & POLLIN) {
            sig.handle_read();
        }
    }

    fprintf(stderr, "[%s] shutting down\n", PROC_NAME);
    exit_seq.run(100);
}