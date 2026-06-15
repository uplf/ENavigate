#ifndef VISION_H
#define VISION_H

#include <Arduino.h>

// ===================== 视觉数据包 =====================
typedef struct
{
    int dx;
    int node_flag;
    char obstacle_type[32];
<<<<<<< HEAD
    int obs_x;          // 障碍物 x 坐标 (0~320)，无则为 -1
=======
>>>>>>> f55c3e92dc1c03812e2be095cfc07c4fc4643342
} VisionData_t;

// ===================== 视觉任务 =====================
void VisionTask(void *pvParameters);

#endif
