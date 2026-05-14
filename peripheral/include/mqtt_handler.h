#ifndef MQTT_HANDLER_H
#define MQTT_HANDLER_H

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// 全局客户端声明
extern WiFiClient espClient;
extern PubSubClient mqttClient;

// 网络状态显示缓存 (保留你原有的逻辑)
extern char netBuffer[64];

enum CarOrient
{
    O_NONE,     // 无指令/空闲
    O_STRAIGHT, // 直行
    O_LEFT,     // 左转
    O_RIGHT,    // 右转[
    O_ARRIVED   // 到达（停车）
};

enum CarAction
{
    A_NONE,     // 无动作
    A_PAUSE,  // 暂停
    A_PROCESS, // 恢复
    A_UTURN      // 掉头
};
// 声明全局指令变量，供主函数读取
extern volatile CarOrient Orient;
extern volatile CarAction Action;
extern volatile bool pcDataReady;
// ======================== 初始化与运行 ========================
void initMQTT();
void reconnectMQTT();
void handleMQTTLoop();

// ======================== 发送(Publish)接口 ========================
// 根据文档定义的小车发信息含义[cite: 1]
void mqtt_send_arrive();
void mqtt_send_obstacle(const String &obstacle_type);
void mqtt_send_repaired();
void mqtt_send_info(const String &info_msg);

// 预留：回应主机查询信息 (暂不开发)[cite: 1]
void mqtt_send_ack(const String &query_type, const String &data);

#endif