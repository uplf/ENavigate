#ifndef CAR_MQTT_CLIENT_H
#define CAR_MQTT_CLIENT_H

#include <Arduino.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// 定义回调函数类型，用于通知主程序执行动作
typedef std::function<void(int step_id, const char *dir, int angle)> OnStepCommand;
typedef std::function<void()> OnStopCommand;

class CarMqttClient
{
public:
    // 构造函数，传入MQTT客户端实例和车辆ID
    CarMqttClient(PubSubClient &mqttClient, const char *carId);

    // 注册回调函数
    void setCallbacks(OnStepCommand stepCb, OnStopCommand stopCb);

    // 初始化（需在MQTT连接成功后调用，用于订阅Topic）
    void subscribeTopics();

    // 接收到MQTT消息时的处理函数（需在PubSubClient的callback中调用）
    void handleMessage(char *topic, byte *payload, unsigned int length);

    // 上报事件：到达节点
    bool publishReached(int step_id);

    // 上报事件：遇到障碍
    bool publishObstacle(int step_id, const char *obs_type);

private:
    PubSubClient &_mqttClient;
    String _carId;
    String _cmdTopic;
    String _eventTopic;

    OnStepCommand _onStepCb;
    OnStopCommand _onStopCb;
};

#endif