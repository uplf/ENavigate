#include <cassert>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <thread>
#include <chrono>

//请确保log_daemon正常运行
#include "mq_wrapper.h"
#include "logger.h"


const char* proc_name="mqtt-mq-sender";


int main(){
    agv::MqSender<agv::MqttPublishMsg>  tx;

    try {
        tx.init(agv::kMqMqttPublish,  agv::kMqttPublishMsgSize);
    } catch (const std::exception& e) {
        fprintf(stderr, "[child] FATAL: %s\n", e.what());
        ::exit(1);
    }
    auto m = agv::MqttPublishMsg::make_angle(1, 5);
    LOG_INFO(proc_name,"send:%d",int(tx.send(m,1)));


}