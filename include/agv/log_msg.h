#pragma once
#include <cstdint>
constexpr const char* MQ_LOG_NAME="/mq_log";
constexpr int MQ_LOG_MAXMSG=64;
constexpr int MQ_LOG_MSGSIZE=256;

enum class LogLevel:uint8_t{
    DEBUG=7,
    INFO=6,
    WARN=4,
    ERROR=3,
    FATAL=2,

};

struct LogMsg{
    LogLevel level;
    char source[32];
    char text[220];
};
static_assert(sizeof(LogMsg)<=MQ_LOG_MSGSIZE,"LogMsg size exceeds MQ_LOG_MSGSIZE");