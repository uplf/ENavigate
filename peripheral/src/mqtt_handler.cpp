#include "mqtt_handler.h"

// ======================== 配置参数 ========================
// MQTT 信息[cite: 1]
const char *MQTT_SERVER = "myxiaxiais.art";
const int MQTT_PORT = 1883;

// 小车1 配置[cite: 1]
const char *MQTT_CLIENT_ID = "esp32_car1_001"; // 确保不和 MQTTX 冲突
const char *MQTT_USERNAME = "agv";             // 按需保留或留空，文档说不需要认证[cite: 1]
const char *MQTT_PASSWORD = "123456";          // 按需保留或留空

const char *MQTT_PUB_TOPIC = "car/1/event"; // 发送主题[cite: 1]
const char *MQTT_SUB_TOPIC = "car/1/cmd";   // 接收主题[cite: 1]

// ======================== 实例与全局变量 ========================
WiFiClient espClient;
PubSubClient mqttClient(espClient);
char netBuffer[64] = "No MQTT Data";

volatile CarOrient Orient = O_NONE;
volatile CarAction Action = A_NONE;
volatile bool pcDataReady = false;

// ======================== 接收(Subscribe)回调 ========================
void mqttCallback(char *topic, byte *payload, unsigned int length)
{
    // 将 payload 转换为字符串
    String msg = "";
    for (unsigned int i = 0; i < length; i++)
    {
        msg += (char)payload[i];
    }

    // 更新屏幕缓存
    msg.substring(0, 60).toCharArray(netBuffer, 64);
    // updateDisplay();

    // 解析 JSON
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, msg);

    if (error)
    {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.f_str());
        return;
    }

    String type = doc["type"].as<String>();
    String param = doc["param"].as<String>();

    // ---------- 根据协议路由指令[cite: 1] ----------
    if (type == "ORIENT")
    {
        // 直角/直行的转向指引[cite: 1]
        if (param == "STRAIGHT")  Orient = O_STRAIGHT;
        else if (param == "LEFT")   Orient = O_LEFT;
        else if (param == "RIGHT") Orient = O_RIGHT;
        else if (param == "ARRIVED") Orient = O_ARRIVED;
        else Orient = O_NONE;
        pcDataReady = true;

    }
    else if (type == "ANGLE")
    {
        // 带角度的转向指引 - 暂不开发[cite: 1]
    }
    else if (type == "QUERY")
    {
        // 主机查询信息 - 暂不开发[cite: 1]
        // 预期收到 STATUS 或 LOG[cite: 1]
    }
    else if (type == "ACTION")
    {
        // 主机行动命令[cite: 1]
        if (param == "PAUSE") Action = A_PAUSE;
        
        else if (param == "PROCESS") Action = A_PROCESS;
        else if (param == "UTURN") Action = A_UTURN;
        else if (param == "REBOOT")
        {
        }
        else Action = A_NONE;
        pcDataReady = true;
    }
}

// ======================== 初始化与连接 ========================
void initMQTT()
{
    mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
    mqttClient.setCallback(mqttCallback);
    // 心跳设为30s[cite: 1]
    mqttClient.setKeepAlive(30);
}

void reconnectMQTT()
{
    while (!mqttClient.connected())
    {


        // 尝试连接
        if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD))
        {
            strcpy(netBuffer, "MQTT Connected");
            Serial.println(netBuffer);

            // 订阅主题[cite: 1]
            if (mqttClient.subscribe(MQTT_SUB_TOPIC))
            {
                strcpy(netBuffer, "Subscribe OK");
            }
            else
            {
                strcpy(netBuffer, "Subscribe Fail");
            }

            // 发送上线提醒 (非协议强制，但有助于调试)
            mqtt_send_info("esp32 car1 online");
        }
        else
        {
            sprintf(netBuffer, "MQTT Fail:%d", mqttClient.state());
            Serial.println(netBuffer);
            delay(2000);
        }
    }
}

void handleMQTTLoop()
{
    if (!mqttClient.connected())
    {
        reconnectMQTT();
    }
    mqttClient.loop();
}

// ======================== 发送(Publish)实现 ========================
// 到达路口或终点指引[cite: 1]
void mqtt_send_arrive()
{
    StaticJsonDocument<128> doc;
    doc["type"] = "ARRIVE";
    doc["param"] = ""; // 空[cite: 1]

    char output[128];
    serializeJson(doc, output);
    mqttClient.publish(MQTT_PUB_TOPIC, output);
}

// 遇见障碍的信号[cite: 1]
void mqtt_send_obstacle(const String &obstacle_type)
{
    StaticJsonDocument<128> doc;
    doc["type"] = "OBSTACLE";
    // 障碍类型不应超过8位[cite: 1]
    doc["param"] = obstacle_type.substring(0, 8);

    char output[128];
    serializeJson(doc, output);
    mqttClient.publish(MQTT_PUB_TOPIC, output);
}

// 障碍恢复的信号[cite: 1]
void mqtt_send_repaired()
{
    StaticJsonDocument<128> doc;
    doc["type"] = "REPAIRED";
    doc["param"] = ""; // 留空[cite: 1]

    char output[128];
    serializeJson(doc, output);
    mqttClient.publish(MQTT_PUB_TOPIC, output);
}

// 其他信息[cite: 1]
void mqtt_send_info(const String &info_msg)
{
    StaticJsonDocument<128> doc;
    doc["type"] = "INFO";
    doc["param"] = info_msg;

    char output[128];
    serializeJson(doc, output);
    mqttClient.publish(MQTT_PUB_TOPIC, output);
}

// 回应主机查询信息 (暂不开发)[cite: 1]
void mqtt_send_ack(const String &query_type, const String &data)
{
    // 预留接口
}