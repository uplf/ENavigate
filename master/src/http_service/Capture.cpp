#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <nlohmann/json.hpp>
using json = nlohmann::json;
#include "shm_manager.h"
#include "mq_wrapper.h"
#include "logger.h"
#include <fcgi_stdio.h>
#include "fcgi_utils.h"
using namespace agv::http;

const char* proc_name = "capture_api";

int main() {
    agv::MqSender<agv::MqttPublishMsg> mq;
    bool mq_ok = false;
    try {
        mq.init(agv::kMqMqttPublish, agv::kMqttPublishMsgSize);
        mq_ok = true;
        dprintf(2, "[capture_api] MQ connected\n");
    } catch (const std::exception& e) {
        dprintf(2, "[capture_api] MQ not available: %s\n", e.what());
    }

    agv::ShmClient shm;
    bool shm_ok = false;
    try {
        shm.attach(500);
        shm_ok = true;
    } catch (const std::exception& e) {
        dprintf(2, "[capture_api] SHM not available: %s\n", e.what());
    }

    while (FCGI_Accept() >= 0) {
        if (handle_preflight()) continue;

        const char* method = getenv("REQUEST_METHOD");
        if (!method || strcmp(method, "POST") != 0) {
            reply_err(405, "只支持 POST 方法");
            continue;
        }

        std::string body = read_body();
        if (body.empty()) {
            reply_err(400, "请求体为空");
            continue;
        }

        std::string car_str = json_str(body, "car");
        if (car_str.empty()) {
            reply_err(400, "缺少 car 字段");
            continue;
        }

        uint8_t car_id = car_id_from_str(car_str);
        if (car_id == 0xFF) {
            reply_err(400, "car 格式错误，应为 C1/C2...");
            continue;
        }

        uint8_t node_id = 0;
        std::string node_str = "N0";
        if (shm_ok) {
            auto car_snap = agv::shm_read_car(shm.ptr(), car_id - 1);
            node_id  = car_snap.current_node_id;
            node_str = node_id_to_str(node_id);
        }

        // 生成时间戳 YYYYMMDDHHmmss
        char ts[16];
        {
            time_t now = time(nullptr);
            struct tm t;
            localtime_r(&now, &t);
            strftime(ts, sizeof(ts), "%Y%m%d%H%M%S", &t);
        }

        if (mq_ok) {
            auto msg = agv::MqttPublishMsg::make_capture(car_id, node_id, ts);
            mq.send(msg, agv::kPrioNormal);
        }

        dprintf(2, "[capture_api] car=%s node=%s ts=%s\n", car_str.c_str(), node_str.c_str(), ts);

        char data[128];
        snprintf(data, sizeof(data),
                 "{\"car\":\"%s\",\"node\":\"%s\",\"ts\":\"%s\"}",
                 car_str.c_str(), node_str.c_str(), ts);
        reply_ok("拍照指令已发送", data);
    }
    return 0;
}
