#include "shared_state.h"
#include "config.h"

// Definitions of all globals declared in shared_state.h
SensorState       g_state;
SemaphoreHandle_t g_stateMutex;
QueueHandle_t     g_serialQueue;
QueueHandle_t     g_errorQueue;
QueueHandle_t     g_mqttPubQueue;

// MqttMessage is defined in mqtt_message.h — forward-declared size here
// so the queue slot size is correct without a circular include.
// sizeof(MqttMessage) = MQTT_MAX_TOPIC_LEN + MQTT_MAX_PAYLOAD_LEN + 2
#define MQTT_MSG_SIZE  (MQTT_MAX_TOPIC_LEN + MQTT_MAX_PAYLOAD_LEN + 2)

void sharedStateInit() {
    g_stateMutex  = xSemaphoreCreateMutex();
    g_serialQueue = xQueueCreate(SERIAL_QUEUE_DEPTH, SERIAL_LINE_MAX);
    g_errorQueue  = xQueueCreate(16,                 sizeof(ErrorEvent));
    g_mqttPubQueue = xQueueCreate(MQTT_PUBLISH_QUEUE_DEPTH, MQTT_MSG_SIZE);

    configASSERT(g_stateMutex);
    configASSERT(g_serialQueue);
    configASSERT(g_errorQueue);
    configASSERT(g_mqttPubQueue);
}
