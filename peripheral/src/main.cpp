#include <Arduino.h>
#include "key.h"
#include <U8g2lib.h>
#include <Wire.h>
#include "drive.h"
#include "encoder.h"

#define SDA_PIN 4
#define SCL_PIN 5
#define LED_PIN 35
#define RXPIN 18
#define TXPIN 17


// 函数声明
void initDisplay();
void updateDisplay();
void initUART();
void readSerial1Data();
void sendData(int msg);

bool led_state = LOW;
bool led_status = false;
unsigned long last_display = 0;
const unsigned long display_interval = 100;

// 状态机定义
enum State
{
  STATE_IDLE,   // 静止状态
  STATE_FOLLOW, // 循线状态
  STATE_TURN    // 转向状态
};

State currentState = STATE_IDLE;

// 巡线控制参数
int dx = 0;            // 偏移量
int node_flag = 0;     // 路口标志
int last_node_flag = 0;
int theta = 90; // 手动定义的转向角度量（示例：90度）
float Kp = 0.8;        // 循线比例系数，根据实际效果调整
int num = 2;

int number = 0;

// 用于接收和显示字符串
char rxBuffer[64] = "";   // 最多存63个字符，最后一个留给 '\0'
int rxIndex = 0;          // 当前存到第几个字符
bool newDataReady = false;
char infoBuffer[32] = "None";

// OLED 实例化 (型号SH1106 大小128x64)
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

void setup() {
  // LED及按键初始化
  pinMode(LED_PIN, OUTPUT);
  encoder_init();

  key_init();

  // 串口初始化
  initUART();
  // 电机驱动初始化
  drive_init();

  // 初始化显示屏
  initDisplay();

  resetEncoders(); 

}

void loop() {
  // 接收串口1字符串
  readSerial1Data();
  

  if (key1_flag){
    key1_flag = false;
    if (currentState == STATE_IDLE){
      currentState = STATE_FOLLOW;
    }
    else{
      currentState = STATE_IDLE;
      drive_setPWM34(0, 0);      // 立即强制物理停止
    }
  }
  if(key2_flag){
    key2_flag = false;
    currentState = STATE_TURN;
  }

  // 状态机：只有当串口解析出新数据时才处理
  // 确保控制频率与传感器数据频率同步，减少抖动
  if (newDataReady){
    switch (currentState){
      case STATE_IDLE:
        // drive_setPWM34(0, 0);
        break;

      case STATE_FOLLOW:
        // 只有在新数据到来时更新电机速度
        // drive_DIFFsetPWM34(-dx * Kp);
        Turn(90);
        currentState = STATE_TURN;

        // 边缘检测：检测到路口跳变
        if (node_flag == 1 && last_node_flag == 0){
      
          // currentState = STATE_TURN;
        
        }
        break;

      case STATE_TURN:
        // 转向状态：执行固定动作
        drive_setPWM34(40, -40);
        if(checkTurnDone()){
          currentState = STATE_IDLE; // 回到循线状态
          drive_setPWM34(0, 0);
        }
        // 边缘检测：路口消失
        if (node_flag == 0 && last_node_flag == 1){
          // currentState = STATE_FOLLOW;
        }
        break;
    }

    // 处理完逻辑后，同步旧状态并清除数据标志
    last_node_flag = node_flag;
    newDataReady = false;
    
  }

  
  // 刷新 OLED 内容
  if (millis() - last_display > display_interval){
    updateDisplay();
    last_display = millis();
  }


}

// ------------------------ 函数定义 ------------------------

// 初始化 OLED 显示屏
void initDisplay() {
  Wire.begin(SDA_PIN, SCL_PIN);
  u8g2.begin();
  u8g2.enableUTF8Print();
}

// OLED显示
void updateDisplay() {
  u8g2.clearBuffer();

  // 画框
  u8g2.drawFrame(0, 0, 128, 64);

  // 显示字符串
  u8g2.setFont(u8g2_font_6x12_tf);
  u8g2.setCursor(5, 12);
  u8g2.print("Status: ");
  if (currentState == STATE_IDLE)
    u8g2.print("IDLE");
  else if (currentState == STATE_FOLLOW)
    u8g2.print("FOLLOW");
  else
    u8g2.print("TURN");

  // 显示数据
  u8g2.setCursor(5, 28);
  // u8g2.printf("dx: %d", dx);
  u8g2.printf("countA: %ld", getEncoder1Count());
  u8g2.setCursor(5, 44);
  // u8g2.printf("node: %d", node_flag);
  u8g2.printf("countB: %ld", getEncoder2Count());

  u8g2.setCursor(5, 60);
  // u8g2.print("Info: ");
  // u8g2.print(infoBuffer);
  u8g2.printf("num: %d", number);

  u8g2.sendBuffer();//刷新oled显示
}

// 串口初始化
void initUART() {
  Serial.begin(115200);
  Serial1.begin(115200, SERIAL_8N1, RXPIN, TXPIN); // GPIO18作为RX，GPIO17作为TX
}

// 接收 Serial1 字符串
void readSerial1Data() {
  while (Serial1.available()) {
    char ch = Serial1.read();
    // Serial.println(ch);

    // 遇到回车或换行，表示一帧数据结束
    if (ch == '\n' || ch == '\r') {
      if (rxIndex > 0) {
        rxBuffer[rxIndex] = '\0';   // 字符串结束符
        rxIndex = 0;                // 准备下一次接收
        if (sscanf(rxBuffer, "%d,%d,%31s", &dx, &node_flag, infoBuffer) >= 2){
          newDataReady = true;
        }

        // 调试输出到电脑串口
        // Serial.print("Received: ");
        // Serial.println(rxBuffer);
      }
    }
    else {
      // 防止数组越界
      if (rxIndex < sizeof(rxBuffer) - 1) {
        rxBuffer[rxIndex++] = ch;
      }
      else {
        // 缓冲区满了，强制结束
        rxBuffer[rxIndex] = '\0';
        rxIndex = 0;
      }
    }
  }
}

// 串口发送函数
void sendData(int msg)
{
 
  Serial1.print(msg);      // 发给外设
  Serial1.print("\r\n");   // 自动换行（很多串口设备需要）
}
