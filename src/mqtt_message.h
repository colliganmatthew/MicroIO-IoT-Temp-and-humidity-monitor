#pragma once
#include <stdint.h>
#include "config.h"

// =============================================================================
// mqtt_message.h  —  the MqttMessage struct passed through g_mqttPubQueue
//
// Any task can build one of these and push it via:
//   mqttEnqueue(topic, payload);
// The MQTT task drains the queue and sends to the broker.
// =============================================================================

struct MqttMessage {
    char    topic[MQTT_MAX_TOPIC_LEN];
    char    payload[MQTT_MAX_PAYLOAD_LEN];
    bool    retain;
    uint8_t qos;
};

// Helper — builds an MqttMessage and pushes it onto g_mqttPubQueue.
// Non-blocking: returns false if the queue is full.
// Declared here, defined in task_mqtt.cpp to avoid circular includes.
bool mqttEnqueue(const char* topic,
                 const char* payload,
                 bool        retain = false,
                 uint8_t     qos    = 0);
