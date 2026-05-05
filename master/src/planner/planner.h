#pragma once
#include <mosquitto.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include <unordered_map>
#include <queue>
#include <limits>
#include <cmath>
#include <algorithm>
//请确保log_daemon正常运行
#include "shm_manager.h"
#include "mq_wrapper.h"
#include "logger.h"
#include "signal_handler.h"
#include "secure_exit.h"

extern const char* proc_name;

namespace agv{

    struct AStarNode{
        uint16_t id;
        float f_cost;
        bool operator>(const AStarNode &other) const
        {
            return f_cost > other.f_cost;
        }
    };

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
        std::vector<uint16_t> find_path(agv::MapData map_data, uint16_t start_node, uint16_t target_node) {
            std::priority_queue<AStarNode, std::vector<AStarNode>, std::greater<AStarNode>> open_set;

            int n = map_data.node_count_;
            const float INF = std::numeric_limits<float>::max();
            constexpr float SOFT_OBSTACLE_PENALTY = 100.0f;

            std::vector<float> g_cost(n, INF);
            std::vector<int> came_from(n, -1);
            std::vector<int> came_edge(n, -1);
            std::vector<bool> closed(n, false);

            if (start_node >= AGV_MAX_NODES || target_node >= AGV_MAX_NODES)
                return {};
            if (start_node >= n || target_node >= n)
                return {};

            g_cost[start_node] = 0.0f;
            open_set.push({start_node, heuristic(map_data.nodes_[start_node], map_data.nodes_[target_node])});

            while (!open_set.empty()) {
                AStarNode current_node = open_set.top();
                open_set.pop();

                uint16_t current = current_node.id;

                if (closed[current])
                    continue;
                closed[current] = true;

                if (current == target_node) {
                    std::vector<uint16_t> path_edges;
                    int cur = target_node;

                    while (came_from[cur] != -1) {
                        path_edges.push_back(static_cast<uint16_t>(came_edge[cur]));
                        cur = came_from[cur];
                    }

                    std::reverse(path_edges.begin(), path_edges.end());
                    return path_edges;
                }

                const AdjEntry& adj = map_data.adj_[current];
                for (int i = 0; i < adj.count; ++i) {
                    uint16_t edge_id = adj.edge_ids[i];
                    if (edge_id >= AGV_MAX_EDGES || edge_id >= map_data.edge_count_)
                        continue;

                    const Edge& edge = map_data.edges_[edge_id];
                    uint16_t next_node = (edge.from_node == current) ? edge.to_node : edge.from_node;

                    if (next_node >= AGV_MAX_NODES || next_node >= n)
                        continue;

                    const Node& next_ptr = map_data.nodes_[next_node];

                    EdgeStatus e_status = edge.status;
                    NodeStatus n_status = next_ptr.status;

                    if (e_status == EdgeStatus::BLOCKED ||
                        e_status == EdgeStatus::FAULT_REPAIR ||
                        n_status == NodeStatus::FAULT)
                        continue;

                    float penalty = 0.0f;
                    if (e_status == EdgeStatus::OCCUPIED ||
                        e_status == EdgeStatus::FAULT_TEMP ||
                        n_status == NodeStatus::OCCUPIED)
                        penalty = SOFT_OBSTACLE_PENALTY;

                    float new_g = g_cost[current] + static_cast<float>(edge.weight) + penalty;

                    if (new_g < g_cost[next_node]) {
                        g_cost[next_node] = new_g;
                        came_from[next_node] = static_cast<int>(current);
                        came_edge[next_node] = static_cast<int>(edge_id);

                        float f = new_g + heuristic(next_ptr, map_data.nodes_[target_node]);
                        open_set.push({next_node, f});
                    }
                }
            }

            return {};
        }
        private:
        static float heuristic(const Node& a, const Node& b) {
            float dx = static_cast<float>(a.x) - static_cast<float>(b.x);
            float dy = static_cast<float>(a.y) - static_cast<float>(b.y);
            return std::sqrt(dx * dx + dy * dy);
        }

        agv::ShmClient _shm;
        agv::MqReceiver _mq_recv;
        agv::MqSender _mq_send;
    };
}