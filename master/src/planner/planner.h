#pragma once
#include <mosquitto.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include <unordered_map>
//请确保log_daemon正常运行
#include "shm_manager.h"
#include "mq_wrapper.h"
#include "logger.h"
#include "signal_handler.h"
#include "secure_exit.h"

const char* proc_name="planner";

namespace agv{

    struct AStarNode{
        uint16_t id;
        float f_cost;
        bool operator>(const AStarNode &other) const
        {
            return f_cost > other.f_cost;
        }
    }

    class planner{
        public:
        bool init(){
            try {
                LOG_INFO(proc_name,"init");
                _shm.attach(500);
                _mq_recv.init(kMqTaskDispatch);
                _mq_send.init(kMqMqttPublish);
            } catch (const std::exception& e) {
                LOG_ERROR(proc_name,"fail to create mq:%s",e.what());
                return false;
            }
            return true;
        }
        void do_cleanup(){
            LOG_INFO(proc_name,"do_cleanup");
        }
        vector<uint16_t> find_path(agv::MapData map_data,uint16_t start_node,uint16_t target_node){
            std::priority_queue<AStarNode, std::vector<AStarNode>, std::greater<AStarNode>> open_set;

            int n = map.node_count_;
            const float INF = std::numeric_limits<float>::max();

            std::vector<float> g_cost(n, INF);
            std::vector<int> came_from(n, -1);
            std::vector<int> came_edge(n, -1);
            std::vector<bool> closed(n, false);
            /*****请重新写被注释掉的函数，需要和本工程的代码匹配。
            auto target_ptr = map.get_node(target_node);
            if (!target_ptr)
                return {};

            g_cost[start_node] = 0.0f;
            open_set.push({start_node, heuristic(*map.get_node(start_node), *target_ptr)});

            while (!open_set.empty())
            {
                int current = open_set.top().node_id;
                open_set.pop();

                if (closed[current])
                    continue;
                closed[current] = true;

                if (current == target_node)
                {
                    // 直接重建路径
                    std::vector<int> path_edges;
                    int cur = target_node;

                    while (came_from[cur] != -1)
                    {
                        int edge_id = came_edge[cur];
                        path_edges.push_back(edge_id);
                        cur = came_from[cur];
                    }

                    std::reverse(path_edges.begin(), path_edges.end());
                    return path_edges;
                }

                for (int edge_id : map.get_neighbors(current))
                {
                    auto edge = map.get_edge(edge_id);
                    if (!edge)
                        continue;

                    int next_node = (edge->from_node == current)
                                        ? edge->to_node
                                        : edge->from_node;

                    auto next_ptr = map.get_node(next_node);
                    if (!next_ptr)
                        continue;

                    auto e_status = edge->status.load(std::memory_order_acquire);
                    auto n_status = next_ptr->status.load(std::memory_order_acquire);

                    if (e_status == models::EdgeStatus::BLOCKED ||
                        e_status == models::EdgeStatus::FAULT_REPAIR ||
                        n_status == models::NodeStatus::FAULT)
                        continue;

                    float penalty = 0.0f;

                    if (e_status == models::EdgeStatus::OCCUPIED ||
                        e_status == models::EdgeStatus::FAULT_TEMP ||
                        n_status == models::NodeStatus::OCCUPIED)
                        penalty = SOFT_OBSTACLE_PENALTY;

                    float new_g = g_cost[current] + edge->weight + penalty;

                    if (new_g < g_cost[next_node])
                    {
                        g_cost[next_node] = new_g;
                        came_from[next_node] = current;
                        came_edge[next_node] = edge_id;

                        float f = new_g + heuristic(*next_ptr, *target_ptr);
                        open_set.push({next_node, f});
                    }
                }
            }

            return {};
            */
        }
        private:
        agv::ShmClient _shm;
        agv::MqReceiver _mq_recv;
        agv::MqSender _mq_send;
    };
}