#include "encoder.h"

// 使用 static 将变量限制在本文件内，防止外部污染
static volatile long count1 = 0;
static volatile long count2 = 0;
long targetCount1 = 0;
long targetCount2 = 0;
bool isTurning = false;

// --- 中断服务函数 (ISR) ---
// 放在 IRAM 中以确保 ESP32 高速响应
void IRAM_ATTR isr1()
{
    // 2倍频逻辑：监听 A 相跳变
    if (digitalRead(E1A) == HIGH)
    {
        (digitalRead(E1B) == LOW) ? count1++ : count1--;
    }
    else
    {
        (digitalRead(E1B) == HIGH) ? count1++ : count1--;
    }
}

void IRAM_ATTR isr2()
{
    if (digitalRead(E2A) == HIGH)
    {
        (digitalRead(E2B) == LOW) ? count2-- : count2++;
    }
    else
    {
        (digitalRead(E2B) == HIGH) ? count2-- : count2++;
    }
}

// --- 统一初始化逻辑 ---
void encoder_init()
{
    // 配置引脚模式
    pinMode(E1A, INPUT_PULLUP);
    pinMode(E1B, INPUT_PULLUP);
    pinMode(E2A, INPUT_PULLUP);
    pinMode(E2B, INPUT_PULLUP);

    // 绑定中断：监听 A 相的所有电平变化 (CHANGE)
    attachInterrupt(digitalPinToInterrupt(E1A), isr1, CHANGE);
    attachInterrupt(digitalPinToInterrupt(E2A), isr2, CHANGE);
}

long getEncoder1Count() { return count1; }
long getEncoder2Count() { return count2; }

void resetEncoders()
{
    count1 = 0;
    count2 = 0;
}

void Turn(float angle)
{
    resetEncoders();
    if(angle>0)
    {
        targetCount1 = (long)(angle * K1);
        targetCount2 = (long)(angle * K2);
    }
    else
    {
        targetCount1 = (long)(angle * K2);
        targetCount2 = (long)(angle * K1);
    }
    isTurning = true;
}

// 仅负责判断是否到达
bool checkTurnDone()
{
    if (!isTurning)
        return true;

    // 使用绝对值判断，无视正反转方向
    bool m1_done = abs(getEncoder1Count()) >= abs(targetCount1)-50;
    bool m2_done = abs(getEncoder2Count()) >= abs(targetCount2)-50;

    if (m1_done && m2_done)
    {
        isTurning = false;
        return true; // 已完成
    }
    return false; // 还在转
}