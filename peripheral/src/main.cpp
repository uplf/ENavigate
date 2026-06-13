#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <U8g2lib.h>

#include "config.h"
#include "drive.h"
#include "encoder.h"
#include "key.h"
#include "mqtt_handler.h"
#include "vision.h"

// ===================== 硬件引脚 =====================
#define SDA_PIN 4
#define SCL_PIN 5
#define LED_PIN 35
#define RXPIN 18
#define TXPIN 17

// ===================== 显示 =====================
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// ===================== 状态枚举 =====================
enum State
{
    STATE_IDLE,
    STATE_FOLLOW,
    STATE_TURN,
    STATE_WAIT_COMMAND,
};

// ===================== 路口阶段（替代 isTurnPending + nodeDetectionLocked + postTurnActive）=====================
enum JunctionPhase
{
    JUNCTION_NONE,       // 正常循线，障碍物立即响应
    JUNCTION_APPROACH,   // 检测到路口 → 走向中心 → 等待指令（障碍物暂存）
    JUNCTION_POST_TURN,  // 转弯/直行后抑制期（障碍物和节点都不响应）
};

// ===================== 系统状态快照（用于 DisplayTask）=====================
typedef struct
{
    State currentState;
    int dx;
    int node_flag;
    char obstacle_type[32];
    long encoder1;
    long encoder2;
    Orient orient;
    int roadnum;
    bool wifiConnected;
    int roadcount;
} SystemStatus_t;

// ===================== 控制上下文（ControlTask 全部可变状态）=====================
typedef struct
{
    bool obstacleSent;               // 障碍已上报，避免重复
    bool dogWasDetected;             // dog/cat/people 停车标记，消失后自动恢复
    bool approachObstaclePending;    // 路口接近期间暂存障碍物
    char approachObstacleType[32];   // 暂存的障碍物类型
} ObstacleCtx_t;

typedef struct
{
    State state;
    Orient orient;
    int roadNum;
    int roadCount;
    int lastNodeFlag;
    JunctionPhase juncPhase;
    bool hasOrientForThisNode;
    ObstacleCtx_t obs;
    long turnEndEnc1;
    long turnEndEnc2;
    uint32_t waitCmdStartMs;
    int noneCnt;                   // "none" 连续帧计数，≥3 才确认消失
} ControlCtx_t;

// ===================== 全局共享资源 =====================
static SystemStatus_t g_systemStatus;
static uint8_t g_resetReason = 0;

QueueHandle_t g_visionQueue;
QueueHandle_t g_commandQueue;
SemaphoreHandle_t g_statusMutex;
QueueHandle_t g_keyQueue;

// ===================== 函数声明 =====================
void initHardware();
void initDisplay();
void initWIFI();

void ControlTask(void *pvParameters);
void MQTTTask(void *pvParameters);
void DisplayTask(void *pvParameters);
void SafetyTask(void *pvParameters);
void KeyTask(void *pvParameters);

void IRAM_ATTR key1_isr();
void IRAM_ATTR key2_isr();

// ===================== 工具函数 =====================
static inline uint32_t getSystemTimeMs()
{
    return xTaskGetTickCount() * portTICK_PERIOD_MS;
}

// ===================== 控制辅助函数 =====================

// 统一转向分发：orient → 驱动指令 + 目标状态
static State dispatchOrient(Orient orient)
{
    switch (orient)
    {
    case O_LEFT:
        Turn(TURN_ANGLE_LEFT);
        drive_setPWM34(TURN_DIFF_PWM, -TURN_DIFF_PWM);
        return STATE_TURN;

    case O_RIGHT:
        Turn(TURN_ANGLE_RIGHT);
        drive_setPWM34(-TURN_DIFF_PWM, TURN_DIFF_PWM);
        return STATE_TURN;

    case O_UTURN:
        Turn(TURN_ANGLE_UTURN);
        drive_setPWM34(-TURN_DIFF_PWM, TURN_DIFF_PWM);
        return STATE_TURN;

    case O_STRAIGHT:
        resetEncoders();
        return STATE_FOLLOW;

    case O_ARRIVED:
        drive_setPWM34(0, 0);
        return STATE_IDLE;

    default:
        drive_setPWM34(0, 0);
        mqtt_send_info("No valid orient, stopped");
        return STATE_IDLE;
    }
}

// 重置路口上下文（进入 IDLE 或新路口时调用）
static void resetJunctionCtx(ControlCtx_t &ctx)
{
    ctx.juncPhase = JUNCTION_NONE;
    ctx.hasOrientForThisNode = false;
    ctx.obs.approachObstaclePending = false;
}

// 统一障碍物停车上报
static void handleObstacleStop(ControlCtx_t &ctx, const char *obsType, bool afterThreshold)
{
    if (!ctx.obs.obstacleSent)
    {
        ctx.obs.obstacleSent = true;
        String label = String(obsType);
        label = (afterThreshold ? "y" : "n") + label;
        mqtt_send_obstacle(label);
        if (strcmp(obsType, "dog") == 0 ||
            strcmp(obsType, "cat") == 0 ||
            strcmp(obsType, "people") == 0)
            ctx.obs.dogWasDetected = true;
    }
    drive_setPWM34(0, 0);
    ctx.state = STATE_IDLE;
    resetJunctionCtx(ctx);

    // 暂时性障碍物（dog/cat/people）：主动刹车，减少滑行距离
    if (ctx.obs.dogWasDetected)
    {
        drive_setPWM34(-30, -30);
        vTaskDelay(pdMS_TO_TICKS(100));
        drive_setPWM34(0, 0);
    }
}

// 路口 orient 决策 + 暂存障碍物判断（FOLLOW 和 WAIT_COMMAND 共用）
static void executeOrientDecision(ControlCtx_t &ctx)
{
    if (ctx.obs.approachObstaclePending)
    {
        if (ctx.orient == O_STRAIGHT)
        {
            // 直行路径上有障碍物 → 停车上报
            handleObstacleStop(ctx, ctx.obs.approachObstacleType, true);
            ctx.orient = O_NONE;
            ctx.hasOrientForThisNode = false;
            ctx.obs.approachObstaclePending = false;
            return;
        }
        // 转弯路径不受直线障碍影响，忽略
        ctx.obs.approachObstaclePending = false;
    }

    ctx.state = dispatchOrient(ctx.orient);
    ctx.orient = O_NONE;
    ctx.hasOrientForThisNode = false;

    if (ctx.state == STATE_FOLLOW)
    {
        // 直行：进入抑制期
        ctx.turnEndEnc1 = 0;
        ctx.turnEndEnc2 = 0;
        ctx.juncPhase = JUNCTION_POST_TURN;
    }
}

// ============================================================
//  setup
// ============================================================
void setup()
{
    Serial.begin(115200);
    g_resetReason = (uint8_t)esp_reset_reason();

    initHardware();

    g_visionQueue = xQueueCreate(5, sizeof(VisionData_t));
    g_commandQueue = xQueueCreate(5, sizeof(Cmd_t));
    g_statusMutex = xSemaphoreCreateMutex();
    g_keyQueue = xQueueCreate(5, sizeof(uint8_t));

    configASSERT(g_visionQueue);
    configASSERT(g_commandQueue);
    configASSERT(g_statusMutex);
    configASSERT(g_keyQueue);

    xTaskCreatePinnedToCore(ControlTask, "ControlTask", 4096, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(VisionTask, "VisionTask", 4096, NULL, 4, NULL, 1);
    xTaskCreatePinnedToCore(MQTTTask, "MQTTTask", 8192, NULL, 3, NULL, 0);
    xTaskCreatePinnedToCore(DisplayTask, "DisplayTask", 4096, NULL, 1, NULL, 1);
    xTaskCreatePinnedToCore(SafetyTask, "SafetyTask", 4096, NULL, 2, NULL, 1);
    xTaskCreatePinnedToCore(KeyTask, "KeyTask", 2048, NULL, 2, NULL, 1);
}

void loop()
{
    vTaskDelete(NULL);
}

// ============================================================
//  硬件初始化
// ============================================================
void initHardware()
{
    pinMode(LED_PIN, OUTPUT);

    encoder_init();
    key_init();
    drive_init();
    resetEncoders();

    Serial1.begin(115200, SERIAL_8N1, RXPIN, TXPIN);

    initDisplay();
    initWIFI();
    initMQTT();

    attachInterrupt(digitalPinToInterrupt(KEY1), key1_isr, FALLING);
    attachInterrupt(digitalPinToInterrupt(KEY2), key2_isr, FALLING);
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
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

// ============================================================
//  ControlTask
// ============================================================
void ControlTask(void *pvParameters)
{
    QueueSetHandle_t queueSet = xQueueCreateSet(5 + 5);
    configASSERT(queueSet);
    xQueueAddToSet(g_visionQueue, queueSet);
    xQueueAddToSet(g_commandQueue, queueSet);

    ControlCtx_t ctx = {};
    ctx.state = STATE_IDLE;

    VisionData_t visionData = {};
    Cmd_t command = {};

    TickType_t xLastWakeTime = xTaskGetTickCount();

    while (1)
    {
        xQueueSelectFromSet(queueSet, pdMS_TO_TICKS(CONTROL_TASK_PERIOD_MS));

        // 1. 清空视觉队列，只保留最新帧
        while (xQueueReceive(g_visionQueue, &visionData, 0) == pdTRUE)
            ;

        // 2. 清空命令队列，逐条处理
        while (xQueueReceive(g_commandQueue, &command, 0) == pdTRUE)
        {
            if (command.orient != O_NONE)
            {
                ctx.orient = command.orient;
                ctx.hasOrientForThisNode = true;
            }

            switch (command.action)
            {
            case A_PAUSE:
                drive_setPWM34(0, 0);
                ctx.state = STATE_IDLE;
                break;

            case A_PROCESS:
                if (ctx.state == STATE_IDLE)
                {
                    ctx.state = STATE_FOLLOW;
                    ctx.lastNodeFlag = 0;
                }
                break;

            case A_UTURN:
                Turn(TURN_ANGLE_UTURN);
                drive_setPWM34(-TURN_DIFF_PWM, TURN_DIFF_PWM);
                ctx.state = STATE_TURN;
                break;

            case A_SETN:
                ctx.roadNum = command.roadnum;
                ctx.roadCount = 0;
                break;

            default:
                break;
            }

            command.action = A_NONE;
            command.orient = O_NONE;
            command.roadnum = 0;
        }

        // ============================================================
        //  障碍检测（分层处理）
        //  A. node_flag=1 → 障碍物在路口 → 急停
        //  B+C. APPROACH 或 WAIT_COMMAND → 暂存，等 orient
        //  D. 正常循线 → 立即停车
        //  POST_TURN 期间全部抑制
        // ============================================================
        if (strcmp(visionData.obstacle_type, "none") != 0 &&
            strcmp(visionData.obstacle_type, "car") != 0)
        {
            ctx.noneCnt = 0; // 障碍物出现，清零消抖计数
            // --- 情况 A：路口标记线高电平 + 非抑制期 → 急停 ---
            if (ctx.state == STATE_FOLLOW &&
                visionData.node_flag == 1 &&
                ctx.juncPhase != JUNCTION_POST_TURN)
            {
                // 若此周期正好是上升沿，先上报 ARRIVE 再上报 OBSTACLE
                if (ctx.juncPhase == JUNCTION_NONE && ctx.lastNodeFlag == 0)
                {
                    ctx.juncPhase = JUNCTION_APPROACH;
                    resetEncoders();
                    ctx.hasOrientForThisNode = false;
                    ctx.obs.approachObstaclePending = false;
                    mqtt_send_arrive();
                    ctx.roadCount++;
                }
                handleObstacleStop(ctx, visionData.obstacle_type,
                    abs(getEncoder1Count()) >= ENCODER_PASS_THRESHOLD);
            }
            // --- 情况 B+C：APPROACH 定长期 或 WAIT_COMMAND 等待期 → 暂存 ---
            else if ((ctx.state == STATE_FOLLOW &&
                      ctx.juncPhase == JUNCTION_APPROACH &&
                      visionData.node_flag == 0) ||
                     ctx.state == STATE_WAIT_COMMAND)
            {
                if (!ctx.obs.approachObstaclePending)
                {
                    ctx.obs.approachObstaclePending = true;
                    strncpy(ctx.obs.approachObstacleType, visionData.obstacle_type,
                            sizeof(ctx.obs.approachObstacleType) - 1);
                    ctx.obs.approachObstacleType[sizeof(ctx.obs.approachObstacleType) - 1] = '\0';
                }
            }
            // --- 情况 D：正常循线 + 非抑制期 → 立即停车 ---
            else if (ctx.state == STATE_FOLLOW &&
                     ctx.juncPhase != JUNCTION_POST_TURN)
            {
                handleObstacleStop(ctx, visionData.obstacle_type, false);
            }
        }
        else if (strcmp(visionData.obstacle_type, "none") == 0)
        {
            ctx.noneCnt++;

            // 连续 3 帧 "none" 才确认障碍物真正消失，防止相机 flicker 误触发
            if (ctx.noneCnt >= 3)
            {
                if (ctx.obs.dogWasDetected)
                {
                    ctx.obs.dogWasDetected = false;
                    mqtt_send_repaired();
                    ctx.lastNodeFlag = visionData.node_flag;
                    ctx.state = STATE_FOLLOW;
                }
                ctx.obs.obstacleSent = false;
                ctx.obs.approachObstaclePending = false;
            }
        }

        // ============================================================
        //  状态机
        // ============================================================
        switch (ctx.state)
        {
        // ---- IDLE ----
        case STATE_IDLE:
            break;

        // ---- FOLLOW ----
        case STATE_FOLLOW:
        {
            static float filteredDx = 0.0f;
            int dx;

            if (ctx.juncPhase == JUNCTION_APPROACH)
            {
                // 走向路口中心期间不响应视觉偏移，直行通过
                filteredDx = 0.0f;
                dx = 0;
            }
            else
            {
                // 正常循线：低通滤波 + 死区 + 限幅
                filteredDx = filteredDx * DX_FILTER_BETA + (float)visionData.dx * DX_FILTER_ALPHA;
                dx = (int)filteredDx;

                if (abs(dx) < DX_DEADZONE)
                    dx = 0;

                if (dx > DX_CLAMP)  dx = DX_CLAMP;
                if (dx < -DX_CLAMP) dx = -DX_CLAMP;
            }

            drive_DIFFsetPWM34(-dx * KP_DEFAULT);

            // 路口上升沿检测
            if (ctx.juncPhase == JUNCTION_NONE &&
                visionData.node_flag == 1 && ctx.lastNodeFlag == 0)
            {
                ctx.juncPhase = JUNCTION_APPROACH;
                resetEncoders();
                ctx.hasOrientForThisNode = false;
                ctx.obs.approachObstaclePending = false;
                mqtt_send_arrive();
                ctx.roadCount++;
            }

            // 道路计数上限检查
            if (ctx.roadNum != 0 && ctx.roadCount >= ctx.roadNum + 1)
            {
                drive_setPWM34(0, 0);
                ctx.state = STATE_IDLE;
                ctx.roadCount = 0;
                ctx.orient = O_NONE;
                resetJunctionCtx(ctx);
                mqtt_send_info("Road count reached");
                break;
            }

            // 通过路口中心点后执行转向决策
            if (getEncoder1Count() > ENCODER_PASS_THRESHOLD &&
                getEncoder2Count() > ENCODER_PASS_THRESHOLD &&
                ctx.juncPhase == JUNCTION_APPROACH)
            {
                if (ctx.hasOrientForThisNode)
                {
                    executeOrientDecision(ctx);
                }
                else
                {
                    drive_setPWM34(0, 0);
                    ctx.waitCmdStartMs = getSystemTimeMs();
                    ctx.state = STATE_WAIT_COMMAND;
                    mqtt_send_info("Waiting for orient at node");
                }
            }

            ctx.lastNodeFlag = visionData.node_flag;

            // 转弯/直行后抑制解锁
            if (ctx.juncPhase == JUNCTION_POST_TURN && visionData.node_flag == 0)
            {
                long d1 = abs(getEncoder1Count() - ctx.turnEndEnc1);
                long d2 = abs(getEncoder2Count() - ctx.turnEndEnc2);
                if (d1 >= POST_TURN_DISTANCE && d2 >= POST_TURN_DISTANCE)
                {
                    ctx.juncPhase = JUNCTION_NONE;
                }
            }

            break;
        }

        // ---- WAIT_COMMAND ----
        case STATE_WAIT_COMMAND:
        {
            if (ctx.hasOrientForThisNode)
            {
                executeOrientDecision(ctx);
            }
            else if ((getSystemTimeMs() - ctx.waitCmdStartMs) >= COMMAND_WAIT_TIMEOUT_MS)
            {
                drive_setPWM34(0, 0);
                ctx.state = STATE_IDLE;
                ctx.obs.approachObstaclePending = false;
                mqtt_send_info("ERROR: orient timeout (>5s)");
            }
            break;
        }

        // ---- TURN ----
        case STATE_TURN:
            if (checkTurnDone())
            {
                ctx.state = STATE_FOLLOW;
                ctx.turnEndEnc1 = 0;
                ctx.turnEndEnc2 = 0;
                resetEncoders();
                ctx.juncPhase = JUNCTION_POST_TURN;
            }
            break;
        }

        // ============================================================
        //  集中写入共享状态，只持锁一次
        // ============================================================
        xSemaphoreTake(g_statusMutex, portMAX_DELAY);
        g_systemStatus.currentState = ctx.state;
        g_systemStatus.dx = visionData.dx;
        g_systemStatus.node_flag = visionData.node_flag;
        strncpy(g_systemStatus.obstacle_type, visionData.obstacle_type,
                sizeof(g_systemStatus.obstacle_type) - 1);
        g_systemStatus.obstacle_type[sizeof(g_systemStatus.obstacle_type) - 1] = '\0';
        g_systemStatus.encoder1 = getEncoder1Count();
        g_systemStatus.encoder2 = getEncoder2Count();
        g_systemStatus.orient = ctx.orient;
        g_systemStatus.roadnum = ctx.roadNum;
        g_systemStatus.roadcount = ctx.roadCount;
        g_systemStatus.wifiConnected = (WiFi.status() == WL_CONNECTED);
        xSemaphoreGive(g_statusMutex);

        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(CONTROL_TASK_PERIOD_MS));
    }
}

// ============================================================
//  MQTTTask
// ============================================================
void MQTTTask(void *pvParameters)
{
    while (1)
    {
        handleMQTTLoop();
        vTaskDelay(pdMS_TO_TICKS(MQTT_TASK_PERIOD_MS));
    }
}

// ============================================================
//  DisplayTask
// ============================================================
void DisplayTask(void *pvParameters)
{
    SystemStatus_t localStatus;

    while (1)
    {
        xSemaphoreTake(g_statusMutex, portMAX_DELAY);
        memcpy(&localStatus, &g_systemStatus, sizeof(SystemStatus_t));
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
        case STATE_WAIT_COMMAND:
            u8g2.print("WAIT_CMD");
            break;
        default:
            u8g2.print("?");
            break;
        }

        u8g2.setCursor(5, 24);
        u8g2.printf("cnt:%d num:%d", localStatus.roadcount, localStatus.roadnum);

        u8g2.setCursor(5, 36);
        u8g2.printf("obs:%s", localStatus.obstacle_type);

        u8g2.setCursor(5, 48);
        u8g2.printf("E1:%ld E2:%ld", localStatus.encoder1, localStatus.encoder2);

        u8g2.setCursor(5, 60);
        u8g2.printf("WiFi:%s", localStatus.wifiConnected ? "OK" : "--");

        u8g2.sendBuffer();

        vTaskDelay(pdMS_TO_TICKS(DISPLAY_TASK_PERIOD_MS));
    }
}

// ============================================================
//  SafetyTask（预留扩展）
// ============================================================
void SafetyTask(void *pvParameters)
{
    while (1)
    {
        /*
         * 预留扩展：
         * 1. 通信超时停车
         * 2. 任务心跳检测
         * 3. 编码器卡死检测
         * 4. 电机保护
         */
        vTaskDelay(pdMS_TO_TICKS(SAFETY_TASK_PERIOD_MS));
    }
}

// ============================================================
//  KeyTask
// ============================================================
void KeyTask(void *pvParameters)
{
    while (1)
    {
        uint8_t key = 0;
        if (xQueueReceive(g_keyQueue, &key, portMAX_DELAY) == pdTRUE)
        {
            if (key == 1)
                mqtt_send_position();
            else if (key == 2)
                mqtt_send_position();
        }
    }
}

// ============================================================
//  ISR
// ============================================================
void IRAM_ATTR key1_isr()
{
    uint8_t key = 1;
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(g_keyQueue, &key, &woken);
    portYIELD_FROM_ISR(woken);
}

void IRAM_ATTR key2_isr()
{
    uint8_t key = 2;
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(g_keyQueue, &key, &woken);
    portYIELD_FROM_ISR(woken);
}
