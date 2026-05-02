#ifndef __KEY_H
#define __KEY_H

#include <Arduino.h>

// 定义引脚宏
#define KEY1 36
#define KEY2 37
#define KEY3 38

// 外部标志位，用于在主程序中检测状态
extern volatile bool key1_flag;
extern volatile bool key2_flag;
extern volatile bool key3_flag;

void key_init(void);

#endif