#ifndef VISION_H
#define VISION_H

#include <Arduino.h>

// ===================== 视觉数据包 =====================
typedef struct
{
    int dx;
    int node_flag;
    char obstacle_type[32];
} VisionData_t;

// ===================== 视觉任务 =====================
void VisionTask(void *pvParameters);

#endif
