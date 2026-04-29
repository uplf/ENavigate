#pragma once
#include <mqueue.h>
#include <cstdio>
#include <cstring>
#include "agv/log_msg.h"

inline mqd_t agv_log_init(){
    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = MQ_LOG_MAXMSG;
    attr.mq_msgsize = MQ_LOG_MSGSIZE;
    attr.mq_curmsgs = 0;

    mqd_t mq = mq_open(MQ_LOG_NAME, O_CREAT | O_WRONLY, 0644, &attr);
    if (mq == (mqd_t)-1) {
        perror("Failed to open message queue");
        return (mqd_t)-1;
    }
    return mq;
}

inline void _agv_log(mqd_t mq, LogLevel level, const char* source, const char* text){
    if (mq == (mqd_t)-1) {
        fprintf(stderr, "Invalid message queue descriptor\n");
        return;
    }
    LogMsg msg;
    msg.level = level;
    strncpy(msg.source, source, sizeof(msg.source) - 1);
    msg.source[sizeof(msg.source) - 1] = '\0';
    strncpy(msg.text, text, sizeof(msg.text) - 1);
    msg.text[sizeof(msg.text) - 1] = '\0';

    if (mq_send(mq, (const char*)&msg, sizeof(msg), 0) == -1) {
        perror("Failed to send log message");
    }
}
