#include <cassert>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <thread>
#include <chrono>
#include <vector>
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
    shm_ptr->map.edge_count_ = 3*2;
    shm_ptr->bipaths.bipath_count_=0;
    std::vector<agv::bipath_pair> edges{
        agv::bipath_pair::create(1, 2, 1, 3*2/2, 10, agv::EdgeStatus::IDLE, "A-B"),
        agv::bipath_pair::create(2, 3, 2, 3*2/2, 15, agv::EdgeStatus::IDLE, "B-C"),
        agv::bipath_pair::create(1, 3, 3, 3*2/2, 15, agv::EdgeStatus::IDLE, "A-C"),
    };
    for(auto & e:edges){
        shm_ptr->map.edges_[e.idA-1] = e.generate_edgeA();
        shm_ptr->map.edges_[e.idB-1] = e.generate_edgeB();
        shm_ptr->bipaths.paths_[e.idA-1]=e;//这里会使用父类吗？？
        shm_ptr->bipaths.bipath_count_++;
    }

    // 初始化邻接表：遍历所有边，将边索引加入两端节点的邻接表
    for (uint16_t i = 0; i < shm_ptr->map.edge_count_; i++) {
        const auto& e = shm_ptr->map.edges_[i];
        uint16_t from_idx = e.from_node - 1;  // node id → 0-based 索引
        uint16_t to_idx   = e.to_node - 1;

        auto& adj_from = shm_ptr->map.adj_[from_idx];
        if (adj_from.count < AGV_MAX_NEIGHBORS) {
            adj_from.edge_ids[adj_from.count++] = i;
        }

        auto& adj_to = shm_ptr->map.adj_[to_idx];
        if (adj_to.count < AGV_MAX_NEIGHBORS) {
            adj_to.edge_ids[adj_to.count++] = i;
        }
    }
    shm_ptr->map.nodes_[0] = agv::Node{1,10,20,agv::NodeStatus::IDLE,"A","d"};
    shm_ptr->map.nodes_[1] = agv::Node{2, 30, 20, agv::NodeStatus::IDLE, "B", "2"};
    shm_ptr->map.nodes_[2] = agv::Node{3, 50, 20, agv::NodeStatus::IDLE, "C", "3"};
}
void init_car(agv::ShmLayout* shm_ptr){
    shm_ptr->cars.car_count_ = 2;
    //car id从1开始
    shm_ptr->cars.cars_[0] = {
        .id              = 1,
        .status          = agv::CarStatus::IDLE,
        .current_node_id = 1,
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
