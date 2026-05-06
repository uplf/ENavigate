#pragma once

#include "../config/config.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace agv {

/// 系统最大节点数（静态拓扑，与 SHM 定义保持一致）
static constexpr int kMaxNodes = AGV_MAX_NODES;

/// MQTT topic 最大长度（含 \0）
static constexpr int kTopicMaxLen = 128;

/// MQTT payload 最大长度（含 \0）
static constexpr int kPayloadMaxLen = 256;

/// 日志消息最大长度（含 \0）
static constexpr int kLogMsgMaxLen = 512;

/// 进程名最大长度（含 \0）
static constexpr int kProcNameMaxLen = 32;

// ─────────────────────────────────────────────────────────────────────────────
// 队列名称常量
// ─────────────────────────────────────────────────────────────────────────────

static constexpr char kMqTaskDispatch[] = "/mq_task_dispatch";
static constexpr char kMqMqttPublish[]  = "/mq_mqtt_publish";

// ─────────────────────────────────────────────────────────────────────────────
// 队列容量参数（mq_open 时使用）
// ─────────────────────────────────────────────────────────────────────────────

/// /mq_task_dispatch：任务量小，深度不需要太大
static constexpr long kTaskDispatchMaxMsg  = 16;
static constexpr long kTaskDispatchMsgSize = 256;   // sizeof(TaskDispatchMsg) < 256

/// /mq_mqtt_publish：决策消息较频繁
static constexpr long kMqttPublishMaxMsg   = 32;
static constexpr long kMqttPublishMsgSize  = 512;   // sizeof(MqttPublishMsg)  < 512


// ─────────────────────────────────────────────────────────────────────────────
// 消息优先级（数字越大优先级越高）
// ─────────────────────────────────────────────────────────────────────────────

enum MsgPriority : unsigned {
    kPrioLow    = 0,
    kPrioNormal = 1,
    kPrioHigh   = 2,   // 取消任务、紧急障碍物等
};

// ─────────────────────────────────────────────────────────────────────────────
// /mq_task_dispatch — 任务调度消息
// ─────────────────────────────────────────────────────────────────────────────

enum class TaskAction : uint8_t {
    kAssign  = 0,   // 新建任务：将小车派往目标节点
    kCancel  = 1,   // 取消任务
    kReplan  = 2,   // 重规划（拓扑变更触发，不改变目标）
};

struct TaskDispatchMsg {
    TaskAction action;

    uint8_t  car_id;            ///< 小车 ID（0xFF = 广播/任意）
    uint8_t  target_node;       ///< 目标节点 ID（kCancel 时忽略）
    uint8_t  immediate;         ///< 1 = 立即生效；0 = 等当前段完成后生效

    //int64_t  timestamp_us;      ///< 发出时间（CLOCK_MONOTONIC，微秒）

    //uint8_t  _pad[2];           ///< 对齐填充，保持结构体大小为 2 的幂次附近

    /// 便捷构造
    static TaskDispatchMsg assign(uint8_t car, uint8_t target, bool imm = true) {
        TaskDispatchMsg m{};
        m.action       = TaskAction::kAssign;
        m.car_id       = car;
        m.target_node  = target;
        m.immediate    = imm ? 1 : 0;
        return m;
    }
    static TaskDispatchMsg cancel(uint8_t car) {
        TaskDispatchMsg m{};
        m.action  = TaskAction::kCancel;
        m.car_id  = car;
        return m;
    }
    static TaskDispatchMsg replan(uint8_t car) {
        TaskDispatchMsg m{};
        m.action  = TaskAction::kReplan;
        m.car_id  = car;
        return m;
    }
};

static_assert(sizeof(TaskDispatchMsg) <= kTaskDispatchMsgSize,
              "TaskDispatchMsg exceeds queue msg size");

// ─────────────────────────────────────────────────────────────────────────────
// /mq_mqtt_publish — MQTT 发布消息
// ─────────────────────────────────────────────────────────────────────────────

enum class MqttCmdType : uint8_t {
    CMD_angle=0,
    CMD_ori=1,
    QUERY=2,
    ACTION=3,
};
std::string mqtt_cmd_type_to_str(MqttCmdType type) {
    switch (type) {
        case MqttCmdType::CMD_angle: return "ANGLE";
        case MqttCmdType::CMD_ori:   return "ORIENT";
        case MqttCmdType::QUERY:     return "QUERY";
        case MqttCmdType::ACTION:    return "ACTION";
        default:                    return "UNKNOWN";
    }
}
struct AngleParam{
    uint16_t angle;
    uint8_t _pad[2];  
};
enum class OriCmd:uint8_t{
    kStraight=0,
    kLeft=1,
    kRight=2,
    kARRIVED=3,
};
std::string ori_cmd_to_str(OriCmd cmd) {
    switch (cmd) {
        case OriCmd::kStraight: return "STRAIGHT";
        case OriCmd::kLeft:     return "LEFT";
        case OriCmd::kRight:    return "RIGHT";
        case OriCmd::kARRIVED: return "ARRIVED";
        default:               return "UNKNOWN";
    }
}
struct OriParam{
    OriCmd cmd;
    uint8_t _pad[3];
};
enum class QueryCmd:uint8_t{
    kStatus=0,
    kLog=1,
};
std::string query_cmd_to_str(QueryCmd cmd) {
    switch (cmd) {
        case QueryCmd::kStatus: return "STATUS";
        case QueryCmd::kLog:    return "LOG";
        default:               return "UNKNOWN";
    }
}
struct QueryParam{
    QueryCmd cmd;
    uint8_t _pad[3];
};
enum class ActionCmd:uint8_t{
    kPause=0,
    kProcess=1,
    kReboot=2,
    kUturn=3,
};
std::string action_cmd_to_str(ActionCmd cmd) {
    switch (cmd) {
        case ActionCmd::kPause:   return "PAUSE";
        case ActionCmd::kProcess: return "PROCESS";
        case ActionCmd::kReboot:  return "REBOOT";
        case ActionCmd::kUturn:   return "UTURN";
        default:                 return "UNKNOWN";
    }
}
struct ActionParam{
    ActionCmd cmd;
    uint8_t _pad[3];
};


struct MqttPublishMsg {
    MqttCmdType cmd_type;

    uint8_t  car_id;
    uint8_t  next_node;         ///< kMove 时的目标节点，其余忽略
    uint8_t  qos;               ///< MQTT QoS（0/1/2）

    int64_t  timestamp_us;

    union{
        AngleParam c_angle;
        OriParam c_ori;
        QueryParam c_query;
        ActionParam c_action;
    } params;
    static MqttPublishMsg make_angle(uint8_t car_id, uint16_t angle) {
        MqttPublishMsg m{};
        m.cmd_type  = MqttCmdType::CMD_angle;
        m.car_id    = car_id;
        m.params.c_angle.angle = angle;
        m.qos       = 2;
        return m;
    }
    static MqttPublishMsg make_ori(uint8_t car_id, OriCmd cmd) {
        MqttPublishMsg m{};
        m.cmd_type  = MqttCmdType::CMD_ori;
        m.car_id    = car_id;
        m.params.c_ori.cmd = cmd;
        m.qos       = 2;
        return m;
    }
    static MqttPublishMsg make_query(uint8_t car_id, QueryCmd cmd) {
        MqttPublishMsg m{};
        m.cmd_type  = MqttCmdType::QUERY;
        m.car_id    = car_id;
        m.params.c_query.cmd = cmd;
        m.qos       = 1;
        return m;
    }
    static MqttPublishMsg make_action(uint8_t car_id, ActionCmd cmd) {
        MqttPublishMsg m{};
        m.cmd_type  = MqttCmdType::ACTION;
        m.car_id    = car_id;
        m.params.c_action.cmd = cmd;
        m.qos       = 2;
        return m;
    }

    void to_mqtt(char* topic_buf, size_t topic_buf_size, char* payload_buf, size_t payload_buf_size) const {
        // 根据 cmd_type 和 car_id 生成 topic
        snprintf(topic_buf, topic_buf_size, "car/%u/cmd", car_id);
        // 根据 cmd_type 和参数生成 payload（示例为 JSON 格式）
        switch (cmd_type) {
            case MqttCmdType::CMD_angle:
                snprintf(payload_buf,payload_buf_size,"{\"param\":%u}", params.c_angle.angle);
                break;
            case MqttCmdType::CMD_ori:
                snprintf(payload_buf, payload_buf_size, "{\"param\":\"%s\"}",
                         ori_cmd_to_str(params.c_ori.cmd).c_str());
                break;
            case MqttCmdType::QUERY:
                snprintf(payload_buf, payload_buf_size, "{\"param\":\"%s\"}",
                         query_cmd_to_str(params.c_query.cmd).c_str());
                break;
            case MqttCmdType::ACTION:
                snprintf(payload_buf, payload_buf_size, "{\"param\":\"%s\"}",
                         action_cmd_to_str(params.c_action.cmd).c_str());
                break;
            default:
                payload_buf[0] = '\0';
        }
    }
};

static_assert(sizeof(MqttPublishMsg) <= kMqttPublishMsgSize,
              "MqttPublishMsg exceeds queue msg size");


} // namespace agv