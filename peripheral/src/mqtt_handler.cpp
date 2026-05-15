#include "mqtt_handler.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"


extern QueueHandle_t g_commandQueue;


const char *MQTT_SERVER = "myxiaxiais.art";
const int MQTT_PORT = 1883;

const char *MQTT_CLIENT_ID ="esp32_car1_001";
const char *MQTT_USERNAME = "agv";
const char *MQTT_PASSWORD = "123456";

const char *MQTT_PUB_TOPIC ="car/1/event";
const char *MQTT_SUB_TOPIC ="car/1/cmd";

WiFiClient espClient;
PubSubClient mqttClient(espClient);
char netBuffer[64] = "No MQTT Data";

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



    Cmd_t cmd;

    cmd.action = A_NONE;

    cmd.orient = O_NONE;
    cmd.roadnum = 0;

    if (type == "ORIENT")
    {
        if (param == "STRAIGHT") cmd.orient = O_STRAIGHT;
        else if (param == "LEFT") cmd.orient = O_LEFT;
        else if (param == "RIGHT") cmd.orient = O_RIGHT;
        else if (param == "ARRIVED")cmd.orient = O_ARRIVED;
        else if (param == "UTURN") cmd.orient = O_UTURN;

        xQueueSend(g_commandQueue,&cmd,0);
    }


    else if (type == "ACTION")
    {
        if (param == "PAUSE")cmd.action = A_PAUSE;
        else if (param == "PROCESS")cmd.action = A_PROCESS;
        else if (param == "UTURN")cmd.action = A_UTURN;

        xQueueSend(g_commandQueue,&cmd,0);
    }
    else if(type == "CNT"){
        cmd.roadnum = param.toInt();
        cmd.action = A_SETN; 

        xQueueSend(g_commandQueue,&cmd,0);
    }
}



void initMQTT()
{
    mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
    mqttClient.setCallback(mqttCallback);
    // 心跳设为30s
    mqttClient.setKeepAlive(30);
}



void reconnectMQTT()
{

    if (mqttClient.connected())
        return;
    

    static uint32_t lastReconnect = 0;
    if (millis() - lastReconnect < 3000)
    {
        return;
    }

    lastReconnect = millis();

    Serial.println("MQTT reconnecting...");

    bool ok = mqttClient.connect(MQTT_CLIENT_ID,MQTT_USERNAME,MQTT_PASSWORD);

    if (ok)
    {
        strcpy(netBuffer,"MQTT Connected");

        Serial.println(netBuffer);

        if (mqttClient.subscribe(MQTT_SUB_TOPIC))
        {
            strcpy(netBuffer,"Subscribe OK");
        }
        else
        {
            strcpy(netBuffer,"Subscribe Fail");
        }

        mqtt_send_info(
            "esp32 car1 online");
    }
    else
    {
        sprintf(netBuffer,"MQTT Fail:%d",mqttClient.state());

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

void mqtt_send_position()
{
    StaticJsonDocument<128> doc;
    doc["type"] = "POSITION";
    doc["param"] = "1-4"; // 

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