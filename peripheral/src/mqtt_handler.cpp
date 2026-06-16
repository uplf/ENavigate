#include "mqtt_handler.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

extern QueueHandle_t g_commandQueue;

const char *MQTT_SERVER = "------";
const int MQTT_PORT = 1883;
const char *MQTT_CLIENT_ID = "esp32_car2_001";
const char *MQTT_USERNAME = "agv";
const char *MQTT_PASSWORD = "------";
const char *MQTT_PUB_TOPIC = "car/2/event";
const char *MQTT_SUB_TOPIC = "car/2/cmd";

WiFiClient espClient;
PubSubClient mqttClient(espClient);
char netBuffer[64] = "No MQTT Data";

void mqttCallback(char *topic, byte *payload, unsigned int length)
{
    String msg = "";
    for (unsigned int i = 0; i < length; i++)
        msg += (char)payload[i];

    msg.substring(0, 60).toCharArray(netBuffer, 64);

    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, msg);
    if (error)
    {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.f_str());
        return;
    }

    const char *type = doc["type"] | "";
    const char *param = doc["param"] | "";

    Cmd_t cmd;
    cmd.action = A_NONE;
    cmd.orient = O_NONE;
    cmd.roadnum = 0;

    if (strcmp(type, "ORIENT") == 0)
    {
        // 【修复】只有 ORIENT 消息才写 orient，ACTION 消息不再覆盖方向
        if (strcmp(param, "STRAIGHT") == 0)
            cmd.orient = O_STRAIGHT;
        else if (strcmp(param, "LEFT") == 0)
            cmd.orient = O_LEFT;
        else if (strcmp(param, "RIGHT") == 0)
            cmd.orient = O_RIGHT;
        else if (strcmp(param, "ARRIVED") == 0)
            cmd.orient = O_ARRIVED;
        else if (strcmp(param, "UTURN") == 0)
            cmd.orient = O_UTURN;

<<<<<<< HEAD
        xQueueSend(g_cmdQ, &cmd, 0);
=======
        xQueueSend(g_commandQueue, &cmd, 0);
>>>>>>> f55c3e92dc1c03812e2be095cfc07c4fc4643342
    }
    else if (strcmp(type, "ACTION") == 0)
    {
        // 【修复】ACTION 消息的 cmd.orient 保持 O_NONE，不污染方向状态
        if (strcmp(param, "PAUSE") == 0)
            cmd.action = A_PAUSE;
        else if (strcmp(param, "PROCESS") == 0)
            cmd.action = A_PROCESS;
        else if (strcmp(param, "UTURN") == 0)
            cmd.action = A_UTURN;

<<<<<<< HEAD
        xQueueSend(g_cmdQ, &cmd, 0);
    }
    else if (strcmp(type, "CNT") == 0)
    {
        cmd.roadnum = doc["param"] | 0;
        cmd.action = A_SETN;
        xQueueSend(g_cmdQ, &cmd, 0);
    }
    else if (strcmp(type, "CAPTURE") == 0)
    {
        const char *node = doc["param"] | "";
        const char *ts = doc["ts"] | "";
        strncpy(g_capNode, node, sizeof(g_capNode) - 1);
        g_capNode[sizeof(g_capNode) - 1] = '\0';
        strncpy(g_capTs, ts, sizeof(g_capTs) - 1);
        g_capTs[sizeof(g_capTs) - 1] = '\0';
        g_capReq = true;
        cmd.action = A_CAPTURE;
        xQueueSend(g_cmdQ, &cmd, 0);
=======
        xQueueSend(g_commandQueue, &cmd, 0);
    }
    else if (strcmp(type, "CNT") == 0)
    {
        // 【修复】param 是整数(如 3)，不能通过 const char* 读取，
        // 否则 ArduinoJson 隐式转换失败返回 "" → atoi("")=0
        cmd.roadnum = doc["param"] | 0;
        cmd.action = A_SETN;
        xQueueSend(g_commandQueue, &cmd, 0);
>>>>>>> f55c3e92dc1c03812e2be095cfc07c4fc4643342
    }
}

void initMQTT()
{
    mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
    mqttClient.setCallback(mqttCallback);
    mqttClient.setKeepAlive(30);
}

void reconnectMQTT()
{
    if (mqttClient.connected())
        return;

    static uint32_t lastReconnect = 0;
    if (millis() - lastReconnect < 3000)
        return;

    lastReconnect = millis();
    Serial.println("MQTT reconnecting...");

    bool ok = mqttClient.connect(MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD);
    if (ok)
    {
        strcpy(netBuffer, "MQTT Connected");
        Serial.println(netBuffer);
        if (mqttClient.subscribe(MQTT_SUB_TOPIC))
            strcpy(netBuffer, "Subscribe OK");
        else
            strcpy(netBuffer, "Subscribe Fail");

        mqtt_send_info("esp32 car1 online");
    }
    else
    {
        sprintf(netBuffer, "MQTT Fail:%d", mqttClient.state());
        Serial.println(netBuffer);
    }
}

void handleMQTTLoop()
{
    reconnectMQTT();
    mqttClient.loop();
}

void mqtt_send_arrive()
{
    StaticJsonDocument<128> doc;
    doc["type"] = "ARRIVE";
    doc["param"] = "";
    char output[128];
    serializeJson(doc, output);
    mqttClient.publish(MQTT_PUB_TOPIC, output);
}

void mqtt_send_obstacle(const String &obstacle_type)
{
    StaticJsonDocument<128> doc;
    doc["type"] = "OBSTACLE";
    doc["param"] = obstacle_type.substring(0, 8);
    char output[128];
    serializeJson(doc, output);
    mqttClient.publish(MQTT_PUB_TOPIC, output);
}

void mqtt_send_repaired()
{
    StaticJsonDocument<128> doc;
    doc["type"] = "REPAIRED";
    doc["param"] = "";
    char output[128];
    serializeJson(doc, output);
    mqttClient.publish(MQTT_PUB_TOPIC, output);
}

void mqtt_send_position()
{
    StaticJsonDocument<128> doc;
    doc["type"] = "POSITION";
<<<<<<< HEAD
    doc["param"] = CAR_POSITION;
=======
    doc["param"] = "2-5";
>>>>>>> f55c3e92dc1c03812e2be095cfc07c4fc4643342
    char output[128];
    serializeJson(doc, output);
    mqttClient.publish(MQTT_PUB_TOPIC, output);
}

void mqtt_send_info(const String &info_msg)
{
    StaticJsonDocument<128> doc;
    doc["type"] = "INFO";
    doc["param"] = info_msg;
    char output[128];
    serializeJson(doc, output);
    mqttClient.publish(MQTT_PUB_TOPIC, output);
}

void mqtt_send_ack(const String &query_type, const String &data)
{
    // 预留接口，暂不实现
}