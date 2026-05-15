#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <U8g2lib.h>

#include "drive.h"
#include "encoder.h"
#include "key.h"
#include "mqtt_handler.h"


#define SDA_PIN 4
#define SCL_PIN 5

#define LED_PIN 35

#define RXPIN 18
#define TXPIN 17

#define CONTROL_TASK_PERIOD_MS 10
#define DISPLAY_TASK_PERIOD_MS 200
#define MQTT_TASK_PERIOD_MS 20
#define SAFETY_TASK_PERIOD_MS 100

const char *WIFI_SSID = "happywaming";
const char *WIFI_PASSWORD = "happywaming2026";

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0,U8X8_PIN_NONE);

enum State
{
  STATE_IDLE,
  STATE_FOLLOW,
  STATE_TURN
};


typedef struct
{
  int dx;
  int node_flag;
  char info[32];

  uint32_t timestamp;
} VisionData_t;

typedef struct
{
  State currentState;

  int dx;
  int node_flag;
  int encoder1;
  int encoder2;
  Orient orient;

  bool wifiConnected;

} SystemStatus_t;

// 全局系统状态
static SystemStatus_t g_systemStatus;



QueueHandle_t g_visionQueue;
QueueHandle_t g_commandQueue;

SemaphoreHandle_t g_statusMutex;
SemaphoreHandle_t g_keysem;

static char rxBuffer[64];
static int rxIndex = 0;


float Kp = 0.3f;
bool isturn = false;
int last_node_flag = 0;



void initHardware();
void initDisplay();
void initWIFI();


//Task
void ControlTask(void *pvParameters);
void VisionTask(void *pvParameters);
void MQTTTask(void *pvParameters);
void DisplayTask(void *pvParameters);
void SafetyTask(void *pvParameters);
void KeyTask(void *pvParameters);

//ISR
void IRAM_ATTR key1_isr();
void IRAM_ATTR key2_isr();



void setup()
{
  Serial.begin(115200);

  initHardware();

  g_visionQueue = xQueueCreate(5,sizeof(VisionData_t));

  g_commandQueue = xQueueCreate(5,sizeof(Cmd_t));

  g_statusMutex = xSemaphoreCreateMutex();

  g_keysem = xSemaphoreCreateBinary();

  if (!g_visionQueue ||!g_commandQueue ||!g_statusMutex ||!g_keysem)
  {
    Serial.println("FreeRTOS object create failed");

    while (1)
    {
      delay(1000);
    }
  }

  //核心控制任务,最高优先级
  xTaskCreatePinnedToCore(ControlTask,"ControlTask",4096,NULL,5,NULL,1);

  //K230串口数据接收
  xTaskCreatePinnedToCore(VisionTask,"VisionTask",4096,NULL,4,NULL,1);

  //MQTT 放Core0避免影响实时控制
  xTaskCreatePinnedToCore(MQTTTask,"MQTTTask",8192,NULL,3,NULL,0);

  //OLED刷新任务 最低优先级
  xTaskCreatePinnedToCore(DisplayTask,"DisplayTask",4096,NULL,1,NULL,1);

  //安全监控任务
  xTaskCreatePinnedToCore(SafetyTask,"SafetyTask",4096,NULL,2,NULL,1);

  //按键处理任务
  xTaskCreatePinnedToCore(KeyTask,"KeyTask",2048,NULL,2,NULL,1);

}

void loop()
{
  vTaskDelete(NULL);
}


void initHardware()
{
  pinMode(LED_PIN, OUTPUT);

  encoder_init();

  key_init();

  drive_init();

  resetEncoders();

  Serial1.begin(115200,SERIAL_8N1,RXPIN,TXPIN);

  initDisplay();

  initWIFI();

  initMQTT();

  attachInterrupt(digitalPinToInterrupt(KEY1),key1_isr,FALLING);
  attachInterrupt(digitalPinToInterrupt(KEY2),key2_isr,FALLING);
  // attachInterrupt(digitalPinToInterrupt(KEY3),key3_isr,FALLING);
}

void initDisplay()
{
  Wire.begin(SDA_PIN, SCL_PIN);
  u8g2.begin();
  u8g2.enableUTF8Print();
}


void initWIFI()
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID,WIFI_PASSWORD);
}


void ControlTask(void *pvParameters)
{
  TickType_t xLastWakeTime;

  xLastWakeTime = xTaskGetTickCount();

  VisionData_t visionData;

  Cmd_t command;

  State currentState = STATE_IDLE;

  Orient orient = O_NONE;

  while (1)
  {
    //接收视觉
    while (xQueueReceive(g_visionQueue,&visionData,0) == pdTRUE){
      xSemaphoreTake(g_statusMutex,portMAX_DELAY);

      g_systemStatus.dx = visionData.dx;
      g_systemStatus.node_flag =visionData.node_flag;

      xSemaphoreGive(g_statusMutex);
    }

    //接收mqtt
    while (xQueueReceive(g_commandQueue,&command,0) == pdTRUE){
      switch (command.action){
      case A_PAUSE:
        drive_setPWM34(0, 0);
        currentState = STATE_IDLE;
        break;

      case A_PROCESS:
        if (currentState == STATE_IDLE)
          currentState = STATE_FOLLOW;
        break;

      case A_UTURN:
        Turn(180);
        drive_setPWM34(-40, 40);
        currentState = STATE_TURN;
        break;

      default:break;
      }

      orient = command.orient;
    }

    switch (currentState){
    case STATE_IDLE:
      break;

    case STATE_FOLLOW:{
      int dx = visionData.dx;

      drive_DIFFsetPWM34(-dx * Kp);

      if (visionData.node_flag == 1 &&last_node_flag == 0){
        resetEncoders();
        isturn = true;
        mqtt_send_arrive();
      }

      if (getEncoder1Count() > 750 &&getEncoder2Count() > 750 &&isturn){
        isturn = false;
        switch (orient){
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
          break;

        case O_ARRIVED:
          drive_setPWM34(0, 0);
          currentState = STATE_IDLE;
          break;

        default:
          drive_setPWM34(0, 0);
          mqtt_send_info("No command at node");
          currentState = STATE_IDLE;
          break;
        }
        orient = O_NONE;
      }

      last_node_flag =
          visionData.node_flag;

      break;
    }

    case STATE_TURN:
      if (checkTurnDone())
      {
        currentState = STATE_FOLLOW;
      }

      break;
    }


    xSemaphoreTake(g_statusMutex,portMAX_DELAY);

    g_systemStatus.currentState = currentState;

    g_systemStatus.encoder1 = getEncoder1Count();

    g_systemStatus.encoder2 = getEncoder2Count();

    g_systemStatus.orient =orient;

    xSemaphoreGive(g_statusMutex);

    vTaskDelayUntil(&xLastWakeTime,pdMS_TO_TICKS(CONTROL_TASK_PERIOD_MS));
  }
}


void VisionTask(void *pvParameters)
{
  VisionData_t visionData;

  while (1)
  {
    while (Serial1.available()){
      char ch = Serial1.read();

      if (ch == '\n' || ch == '\r'){
        if (rxIndex > 0){
          rxBuffer[rxIndex] = '\0';
          rxIndex = 0;
          if (sscanf(rxBuffer,"%d,%d,%31s",&visionData.dx,&visionData.node_flag,visionData.info) >= 2){
            visionData.timestamp =millis();


            xQueueSend(g_visionQueue,&visionData,0);
          }
        }
      }
      else{
        if (rxIndex <sizeof(rxBuffer) - 1){
          rxBuffer[rxIndex++] = ch;
        }
        else{
          rxIndex = 0;
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(2));
  }
}


void MQTTTask(void *pvParameters)
{
  while (1)
  {

    bool wifiOK =(WiFi.status() == WL_CONNECTED);

    xSemaphoreTake(g_statusMutex,portMAX_DELAY);

    g_systemStatus.wifiConnected = wifiOK;

    xSemaphoreGive(g_statusMutex);

    if (wifiOK)
    {
      handleMQTTLoop();
    }

    vTaskDelay(pdMS_TO_TICKS(MQTT_TASK_PERIOD_MS));
  }
}


void DisplayTask(void *pvParameters)
{
  SystemStatus_t localStatus;

  while (1)
  {

    xSemaphoreTake(g_statusMutex,portMAX_DELAY);

    memcpy(&localStatus,&g_systemStatus,sizeof(SystemStatus_t));

    xSemaphoreGive(g_statusMutex);

    u8g2.clearBuffer();
    u8g2.drawFrame(0, 0, 128, 64);
    u8g2.setFont(u8g2_font_6x12_tf);
    u8g2.setCursor(5, 12);

    u8g2.print("State: ");
    switch (localStatus.currentState)
    {
    case STATE_IDLE:
      u8g2.print("IDLE");
      break;

    case STATE_FOLLOW:
      u8g2.print("FOLLOW");
      break;

    case STATE_TURN:
      u8g2.print("TURN");
      break;
    }

    u8g2.setCursor(5, 28);

    u8g2.printf("dx:%d node:%d",localStatus.dx,localStatus.node_flag);

    u8g2.setCursor(5, 44);

    u8g2.printf("E1:%d",localStatus.encoder1);

    u8g2.setCursor(5, 60);

    u8g2.printf("E2:%d",localStatus.encoder2);

    u8g2.sendBuffer();

    vTaskDelay(pdMS_TO_TICKS(DISPLAY_TASK_PERIOD_MS));
  }
}


void SafetyTask(void *pvParameters)
{
  while (1)
  {
    /*
     * 可加入：
     * 1. 通信超时停车
     * 2. 任务心跳检测
     * 3. CPU占用统计
     * 4. 编码器异常检测
     * 5. 电机保护
     */

    vTaskDelay(pdMS_TO_TICKS(SAFETY_TASK_PERIOD_MS));
  }
}



void KeyTask(void *pvParameters)
{
  while (1)
  {
    if (xSemaphoreTake(g_keysem,portMAX_DELAY) == pdTRUE)
    {
      Cmd_t cmd;
      cmd.action = A_PROCESS;
      cmd.orient = O_RIGHT;
      xQueueSend(g_commandQueue,&cmd,0);
    }

  }
}


void IRAM_ATTR key1_isr()
{
  BaseType_t Woken = pdFALSE;
  xSemaphoreGiveFromISR(g_keysem,&Woken);
  portYIELD_FROM_ISR(Woken);
}

void IRAM_ATTR key2_isr()
{
  BaseType_t Woken = pdFALSE;
  xSemaphoreGiveFromISR(g_keysem,&Woken);
  portYIELD_FROM_ISR(Woken);
}