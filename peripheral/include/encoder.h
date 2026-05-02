#ifndef ENCODER_H
#define ENCODER_H

#include <Arduino.h>

#define E1A 16
#define E1B 48
#define E2A 47
#define E2B 21

#define K1 (1500.0f / 360.0f)
#define K2 (-1300.0f / 360.0f)

extern long targetCount1;
extern long targetCount2;
extern bool isTurning;

void encoder_init();

// 获取计数值
long getEncoder1Count();
long getEncoder2Count();

// 重置计数值
void resetEncoders();

void Turn(float angle);
bool checkTurnDone();

#endif