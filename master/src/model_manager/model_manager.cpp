#include <cassert>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <thread>
#include <chrono>

//请确保log_daemon正常运行
#include "shm_manager.h"
#include "mq_wrapper.h"
#include "logger.h"
#include "signal_handler.h"
#include "secure_exit.h"


const char* proc_name="model-mng";

//初始状态 TODO
void init_map(agv::ShmLayout* shm_ptr){
    shm_ptr->map.node_count_ = 3;
    shm_ptr->map.edge_count_ = 2;

    shm_ptr->map.edges_[0] = {0, 0, 1, 10, agv::EdgeStatus::IDLE};
    shm_ptr->map.edges_[1] = {1, 1, 2, 15, agv::EdgeStatus::IDLE};

    shm_ptr->map.nodes_[0] = agv::Node{0,10,20,agv::NodeStatus::IDLE,"A","d"};
    shm_ptr->map.nodes_[1] = agv::Node{1, 30, 20, agv::NodeStatus::IDLE, "B", "2"};
    shm_ptr->map.nodes_[2] = agv::Node{2, 50, 20, agv::NodeStatus::IDLE, "C", "3"};
}
void init_car(agv::ShmLayout* shm_ptr){
    shm_ptr->cars.car_count_ = 2;
    //car id从1开始
    shm_ptr->cars.cars_[0] = {
        .id              = 1,
        .status          = agv::CarStatus::IDLE,
        .current_node_id = 0,
        .current_task_id = 0,
        .target_node_id  = 0,
        .last_start_node_id=1,
        .path_len        = 0,
    };
    shm_ptr->cars.cars_[1] = {
        .id              = 2,
        .status          = agv::CarStatus::IDLE,
        .current_node_id = 2,
        .current_task_id = 0,
        .target_node_id  = 0,
        .last_start_node_id=2,
        .path_len        = 0,
    };
}


int main(){
    LOG_INFO(proc_name,"begin");
    agv::SignalHandler sig(proc_name);
    try {
        sig.init();
    } catch (const std::exception& e) {
        LOG_FATAL(proc_name,"%s",e.what());
        return 1;
    }

    //初始化业务资源
    agv::MqOwner owner_mq;
    try {
        owner_mq.create_all();
    } catch (const std::exception& e) {
        LOG_ERROR(proc_name,"fail to create mq:%s",e.what());
        return 1;
    }
    agv::ShmOwner owner_shm;
    try{
        owner_shm.create_and_init();
        agv::ShmLayout* shm_ptr = owner_shm.ptr();
        init_map(shm_ptr);
        init_car(shm_ptr);
        //SUG检查逻辑
    }catch (const std::exception& e) {
        LOG_ERROR(proc_name,"fail to create shm:%s",e.what());
        return 1;
    }
    

    //注册退出清理序列
    agv::SecureExit exit_seq(proc_name);
    exit_seq.add_cleanup("finish",[&]{LOG_INFO(proc_name,"finish unlinking mq");});
    exit_seq.add_cleanup("unlink-mq",[&]{owner_mq.unlink_all();});

    //组建 poll 监听数组
    constexpr int FD_SIG = 0;

    struct pollfd fds[2];
    fds[FD_SIG].fd     = sig.get_fd();
    fds[FD_SIG].events = POLLIN;

    LOG_INFO(proc_name,"enter-poll-loop");

    //poll 主循环
    while (!sig.shutdown_requested()) {
        int nfds = sizeof(fds) / sizeof(fds[0]);
        int ret  = ::poll(fds, nfds, -1);  // 无限等待，全事件驱动

        if (ret < 0) {
            if (errno == EINTR) continue;   // 被打断，重试
            LOG_ERROR(proc_name,"poll:%s",strerror(errno));
            break;
        }

        if (fds[FD_SIG].revents & POLLIN) {
            sig.handle_read();
            continue;
        }

    }
    LOG_INFO(proc_name,"shutdown-requested");
    exit_seq.run(200);
    LOG_INFO(proc_name,"shutdown-finished");
}