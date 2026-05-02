/**
 * main.cpp — mqtt_publisher 进程
 *
 * 职责：
 *   阻塞于 mq_mqtt_publish，收到命令后调用 libmosquitto 发布到 broker。
 *
 * 数据流：
 *   planner / mqtt_subscriber
 *       → mq_mqtt_publish (POSIX MQ)
 *       → [本进程]
 *       → mosquitto_publish
 *       → broker
 *       → 从机小车
 *
 * 调试模式（本文件当前状态）：
 *   不从 SHM 读实际数据，只把收到的 MqttPublishMsg 格式化后发布，
 *   同时订阅 car/+/event 回显收到的任何消息，便于端到端联调。
 *
 * 命令行：
 *   mqtt_publisher <broker_host> [broker_port]
 *   mqtt_publisher localhost
 *   mqtt_publisher 192.168.1.100 1883
 *
 * 编译：
 *   g++ -std=c++17 \
 *       -I../../ -I../../lib \
 *       -o mqtt_publisher main.cpp \
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
#include <unistd.h>

static const char* PROC_NAME = "mqtt_publisher";

// ─────────────────────────────────────────────────────────────────────────────
// Publisher MQTT 客户端
// ─────────────────────────────────────────────────────────────────────────────

class PublisherClient : public agv::MqttClientBase {
public:
    /**
     * 把 MqttPublishMsg 序列化为 topic / payload 并发布。
     * 在 poll 主循环的 MQ 分支中调用。
     */
    bool publish(const agv::MqttPublishMsg& msg) {
        if (!is_connected()) {
            fprintf(stderr, "[%s] not connected, drop cmd type=%u car=%u\n",
                    PROC_NAME,
                    static_cast<uint8_t>(msg.type), msg.car_id);
            return false;
        }

        char topic  [128] = {};
        char payload[512] = {};
        msg.to_mqtt(topic, sizeof(topic), payload, sizeof(payload));

        int rc = mosquitto_publish(
            mosq(),
            /*mid=*/nullptr,
            topic,
            static_cast<int>(strlen(payload)),
            payload,
            msg.qos,
            /*retain=*/false);

        if (rc != MOSQ_ERR_SUCCESS) {
            fprintf(stderr, "[%s] publish failed topic=%s rc=%d (%s)\n",
                    PROC_NAME, topic, rc, mosquitto_strerror(rc));
            return false;
        }

        fprintf(stderr, "[%s] publish → %s  %s\n", PROC_NAME, topic, payload);
        return true;
    }

protected:
    // 连接成功后订阅回显 topic（调试用）
    void on_connect(int rc) override {
        if (rc != 0) return;

        // 订阅从机所有事件，回显到 stderr，方便看链路是否通
        mosquitto_subscribe(mosq(), nullptr, "car/+/event", 0);
        fprintf(stderr, "[%s] subscribed car/+/event (debug echo)\n", PROC_NAME);
    }

    void on_message(const mosquitto_message* msg) override {
        fprintf(stderr, "[%s] ← echo  topic=%-24s  payload=%.*s\n",
                PROC_NAME,
                msg->topic,
                msg->payloadlen,
                static_cast<const char*>(msg->payload));
    }
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

    // ── 2. MQTT 客户端初始化并连接 ────────────────────────────────
    PublisherClient mqtt;
    try {
        mqtt.init("agv-publisher");
        mqtt.connect(host, port);
    } catch (const std::exception& e) {
        fprintf(stderr, "[%s] FATAL mqtt: %s\n", PROC_NAME, e.what());
        return 1;
    }

    // ── 3. 打开 MQ 接收端 ─────────────────────────────────────────
    agv::MqReceiver<agv::MqttPublishMsg> mq;
    try {
        mq.init(agv::kMqMqttPublish, 10, agv::kMqttPublishMsgSize);
    } catch (const std::exception& e) {
        fprintf(stderr, "[%s] FATAL mq: %s\n", PROC_NAME, e.what());
        return 1;
    }

    // ── 4. 退出清理 ───────────────────────────────────────────────
    agv::GracefulExit exit_seq(PROC_NAME);
    exit_seq.add_cleanup("mq_close", [&] { mq.close(); });

    // ── 5. poll 主循环 ────────────────────────────────────────────
    enum { FD_SIG = 0, FD_MQ = 1, FD_COUNT };
    struct pollfd fds[FD_COUNT];
    fds[FD_SIG].fd = sig.get_fd(); fds[FD_SIG].events = POLLIN;
    fds[FD_MQ ].fd = mq.get_fd(); fds[FD_MQ ].events = POLLIN;

    fprintf(stderr, "[%s] ready, waiting for commands on %s\n",
            PROC_NAME, agv::kMqMqttPublish);

    while (!sig.shutdown_requested()) {
        int ret = ::poll(fds, FD_COUNT, -1);
        if (ret < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "[%s] poll error: %s\n", PROC_NAME, strerror(errno));
            break;
        }

        if (fds[FD_SIG].revents & POLLIN) {
            sig.handle_read();
            continue;
        }

        if (fds[FD_MQ].revents & POLLIN) {
            agv::MqttPublishMsg msg{};
            unsigned prio = 0;
            while (mq.receive(msg, prio)) {
                mqtt.publish(msg);
            }
        }
    }

    fprintf(stderr, "[%s] shutting down\n", PROC_NAME);
    exit_seq.run(100);
}