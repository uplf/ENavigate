#include <Arduino.h>
#include "key.h"
#include <U8g2lib.h>
#include <Wire.h>
#include "drive.h"
#include "encoder.h"
#include "mqtt_handler.h"

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
void initWiFi();


bool led_state = LOW;
bool led_status = false;
unsigned long last_display = 0;
const unsigned long display_interval = 100;

// WiFi 信息
const char *WIFI_SSID = "happywaming";
const char *WIFI_PASSWORD = "happywaming2026";


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
float Kp = 0.3;        // 循线比例系数，根据实际效果调整
int num = 0;
int number = 0;

bool isturn = false;

// 用于接收和显示字符串
char rxBuffer[64] = "";   // 最多存63个字符，最后一个留给 '\0'
int rxIndex = 0;          // 当前存到第几个字符
bool newDataReady = false;
char infoBuffer[32] = "None";
bool pause_flag = false;
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

  initWiFi();
  initMQTT();
}

void loop() {
  // 接收串口1字符串
  // handleMQTTLoop();


  readSerial1Data();
  

  if (key1_flag){
     key1_flag = false;
    // if (currentState == STATE_IDLE){
    //   currentState = STATE_FOLLOW;
    // }
    // else{
    //   currentState = STATE_IDLE;
    //   drive_setPWM34(0, 0);      // 立即强制物理停止
    // }
    Orient = O_RIGHT;
  }
  if(key2_flag){
    key2_flag = false;
    currentState = STATE_TURN;
  }

  // 状态机：只有当串口解析出新数据时才处理
  // 确保控制频率与传感器数据频率同步，减少抖动
  if (newDataReady||pcDataReady){
    // if(num ==2){
    //   Action = A_UTURN;
    //   Orient = O_LEFT;
    //   num = 0;  

    //     }
    if(pcDataReady){
      switch (Action){
        case A_PAUSE:
          drive_setPWM34(0, 0);      // 停止
          currentState = STATE_IDLE; // 切换到空闲状态
          pause_flag = true;
          break;
        case A_PROCESS:
          if (currentState == STATE_IDLE){
            currentState = STATE_FOLLOW; // 恢复巡线
            pause_flag = false;
          }
          else{
            mqtt_send_info("Already in process");
          }
          break;
        case A_UTURN:
          Turn(180);       // 执行掉头
          currentState = STATE_TURN; // 切换到转向状态
          drive_setPWM34(-40, 40); // 掉头时左右反转
          break;
        default:break;
      }
      Action = A_NONE; // 重置指令
      pcDataReady = false;
    }
    if(newDataReady){
      // 根据当前状态执行相应逻辑
      switch (currentState){
        case STATE_IDLE:
          if((Orient != O_NONE)&&(!pause_flag)){
            mqtt_send_info("Start Moving");
            currentState = STATE_FOLLOW;
          }
          break;

        case STATE_FOLLOW:
          drive_DIFFsetPWM34(-dx * Kp);
          if (node_flag == 1 && last_node_flag == 0){
            resetEncoders(); // 到达路口，重置编码器计数
            num++;
            isturn = true; // 标记正在转向
          }
          if(getEncoder1Count() > 750&&getEncoder2Count() > 750&&isturn){ 
            isturn = false; // 转向完成
            mqtt_send_arrive();
            switch (Orient){
              case O_LEFT:
                Turn(90);
                drive_setPWM34(40, -40);
                currentState = STATE_TURN;
                break;

              case O_RIGHT:
                Turn(-90);
                drive_setPWM34(-40, 40);
                currentState = STATE_TURN;
                break;

              case O_STRAIGHT:
                  // 直行不转向，保持当前PWM，通常不需要进入 TURN 状态
                  // currentState = STATE_DRIVE; // 保持巡线或行驶状态
                break;

              case O_ARRIVED:
                drive_setPWM34(0, 0);      // 按照协议要求停车[cite: 1]
                currentState = STATE_IDLE; // 切换到空闲/停止状态
                break;

              default:
              // 协议要求：如果在路口没有收到信息（即 Orient 为空），请停车并发送 INFO[cite: 1]
                drive_setPWM34(0, 0);
                mqtt_send_info("No command at node");
                currentState = STATE_IDLE;
                break;
            }
            Orient = O_NONE; // 重置指令，防止重复触发
          }
          break;

        case STATE_TURN:
          if(checkTurnDone()){
            currentState = STATE_FOLLOW; // 回到循线状态
          }
          break;
      }
      last_node_flag = node_flag;
      newDataReady = false;
    }
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
  u8g2.printf("Orient: %d", Orient);
  u8g2.setCursor(5, 44);
  // u8g2.printf("node: %d", node_flag);
  u8g2.printf("conter1: %ld", getEncoder1Count());

  u8g2.setCursor(5, 60);
  // u8g2.print("Info: ");
  // u8g2.print(infoBuffer);
  u8g2.printf("counter2: %ld",getEncoder2Count());

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

void initWiFi()
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int retry = 0;

  while (WiFi.status() != WL_CONNECTED)
  {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x12_tf);
    u8g2.drawStr(0, 10, "Connecting WiFi...");
    u8g2.sendBuffer();

    delay(500);
    retry++;

    if (retry > 40)
    {
      strcpy(netBuffer, "WiFi Failed");
      return;
    }
  }

  strcpy(netBuffer, "WiFi OK");
}


