#include "CarMqttClient.h"

CarMqttClient::CarMqttClient(PubSubClient &mqttClient, const char *carId)
    : _mqttClient(mqttClient), _carId(carId)
{
    // 动态生成对应的Topic
    _cmdTopic = String("car/") + _carId + "/cmd";
    _eventTopic = String("car/") + _carId + "/event";
    _onStepCb = nullptr;
    _onStopCb = nullptr;
}

void CarMqttClient::setCallbacks(OnStepCommand stepCb, OnStopCommand stopCb)
{
    _onStepCb = stepCb;
    _onStopCb = stopCb;
}

void CarMqttClient::subscribeTopics()
{
    _mqttClient.subscribe(_cmdTopic.c_str());
    Serial.printf("Subscribed to: %s\n", _cmdTopic.c_str());
}

void CarMqttClient::handleMessage(char *topic, byte *payload, unsigned int length)
{
    // 校验Topic是否匹配
    if (String(topic) != _cmdTopic)
        return;

    // 解析JSON (分配足够的内存池)
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, payload, length);

    if (error)
    {
        Serial.print("deserializeJson() failed: ");
        Serial.println(error.c_str());
        return;
    }

    const char *type = doc["type"];
    if (type == nullptr)
        return;

    // 路由分发
    if (strcmp(type, "STEP") == 0)
    {
        if (_onStepCb)
        {
            int step_id = doc["step_id"];
            const char *dir = doc["action"]["dir"];
            int angle = doc["action"]["angle"];
            _onStepCb(step_id, dir, angle);
        }
    }
    else if (strcmp(type, "STOP") == 0)
    {
        if (_onStopCb)
        {
            _onStopCb();
        }
    }
}

bool CarMqttClient::publishReached(int step_id)
{
    StaticJsonDocument<128> doc;
    doc["type"] = "REACHED";
    doc["step_id"] = step_id;

    char buffer[128];
    serializeJson(doc, buffer);
    return _mqttClient.publish(_eventTopic.c_str(), buffer);
}

bool CarMqttClient::publishObstacle(int step_id, const char *obs_type)
{
    StaticJsonDocument<128> doc;
    doc["type"] = "OBSTACLE";
    doc["step_id"] = step_id;
    doc["obs_type"] = obs_type;

    char buffer[128];
    serializeJson(doc, buffer);
    return _mqttClient.publish(_eventTopic.c_str(), buffer);
}