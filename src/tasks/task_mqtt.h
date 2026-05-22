#pragma once
#include <stdint.h>
#include "config.h"

// Message type used by the publish queue
struct MqttMessage {
    char    topic[MQTT_MAX_TOPIC_LEN];
    char    payload[MQTT_MAX_PAYLOAD_LEN];
    bool    retain;
    uint8_t qos;
};

// Push a message onto g_mqttPubQueue from any task. Non-blocking.
bool mqttEnqueue(const char* topic,
                 const char* payload,
                 bool        retain = false,
                 uint8_t     qos    = 0);

void taskMqttStart();
void taskMqttSelfTest();