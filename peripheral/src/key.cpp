#include "key.h"

// 定义标志位
volatile bool key1_flag = false;
volatile bool key2_flag = false;
volatile bool key3_flag = false;

// 记录上一次中断时间，用于消抖
static unsigned long last_time1 = 0;
static unsigned long last_time2 = 0;
static unsigned long last_time3 = 0;
const unsigned long debounce_delay = 50;

// KEY1 中断函数
void IRAM_ATTR isr_key1()
{
    unsigned long now = millis();
    if (now - last_time1 > debounce_delay)
    {
        key1_flag = true;
    }
    last_time1 = now;
}

// KEY2 中断函数
void IRAM_ATTR isr_key2()
{
    unsigned long now = millis();
    if (now - last_time2 > debounce_delay)
    {
        key2_flag = true;
    }
    last_time2 = now;
}

// KEY3 中断函数
void IRAM_ATTR isr_key3()
{
    unsigned long now = millis();
    if (now - last_time3 > debounce_delay)
    {
        key3_flag = true;
    }
    last_time3 = now;
}

void key_init(void)
{
    pinMode(KEY1, INPUT_PULLUP);
    pinMode(KEY2, INPUT_PULLUP);
    pinMode(KEY3, INPUT_PULLUP);

    // 绑定中断
    attachInterrupt(digitalPinToInterrupt(KEY1), isr_key1, FALLING);
    attachInterrupt(digitalPinToInterrupt(KEY2), isr_key2, FALLING);
    attachInterrupt(digitalPinToInterrupt(KEY3), isr_key3, FALLING);
}