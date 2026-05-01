#pragma once

#include "../config/config.h"
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace models{
    enum class CarStatus : uint8_t{
        IDLE,
        MOVING,
        FAULT,
        OFFLINE,
        WAIT,
    };
    struct car{
        uint8_t id;
        CarStatus status;
        uint8_t current_node_id;
        uint8_t current_task_id;
        uint8_t target_node_id;
        uint16_t path_stack[AGV_MAX_PATHLEN];
    };

    enum class NodeStatus : uint8_t{
        IDLE,
        OCCUPIED,
        FAULT,
    };

    enum class EdgeStatus : uint8_t{
        IDLE,
        OCCUPIED,
        BLOCKED,
        FAULT_TEMP,
        FAULT_REPAIR
    };
    struct Node{
        uint16_t id;
        uint16_t x, y;
        NodeStatus status;
        char name[AGC_MAX_NAME];
        char label[AGC_MAX_LABEL];
    };
    struct Edge{
        uint16_t id;
        uint16_t from_node;
        uint16_t to_node;
        uint16_t weight;
        EdgeStatus status;
    };

    struct MapData {
        Node nodes_[AGV_MAX_NODES];
        Edge edges_[AGV_MAX_PATHS];
        uint16_t node_count_;
        uint16_t edge_count_;
        // 邻接表
        uint16_t adj_list_[AGV_MAX_NODES][AGV_MAX_NODES];
    };
    struct CarData{
        Car cars_[AGV_MAX_CARS];
        uint16_t car_count_;
    }

}