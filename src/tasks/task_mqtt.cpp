#include "task_mqtt.h"
#include "../shared_state.h"
#include "../config.h"
#include "../debug.h"
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <string.h>

// =============================================================================
// task_mqtt.cpp  —  MQTT broker connection + telemetry publishing
//
// Responsibilities:
//   1. Connect to broker (with exponential back-off reconnection).
//   2. Publish sensor telemetry every g_state.mqttIntervalMs ms.
//   3. Drain g_mqttPubQueue for on-demand publishes (e.g. from SCPI MQTT:PUB).
//   4. Call client.loop() every iteration to service keepalive and inbound.
//
// The task checks g_state.mqttEnabled before each periodic publish.
// Setting MQTT:EN OFF via SCPI silences telemetry without stopping the task.
// =============================================================================

// PubSubClient operates on Core 0 alongside the WiFi stack
static WiFiClient   s_wifiClient;
static PubSubClient s_client(s_wifiClient);

// mqttEnqueue() — defined here, declared in mqtt_message.h
// Any task calls this to push a message for the MQTT task to send.
bool mqttEnqueue(const char* topic, const char* payload,
                 bool retain, uint8_t qos) {
    if (!g_mqttPubQueue) return false;
    MqttMessage msg;
    strlcpy(msg.topic,   topic,   sizeof(msg.topic));
    strlcpy(msg.payload, payload, sizeof(msg.payload));
    msg.retain = retain;
    msg.qos    = qos;
    bool ok = (xQueueSend(g_mqttPubQueue, &msg, 0) == pdTRUE);
    DBG("mqtt", "mqttEnqueue [%s] -> %s", topic, ok ? "queued" : "FULL");
    return ok;
}

// ─── Inbound message callback ─────────────────────────────────────────────────
// Called by PubSubClient::loop() when a subscribed message arrives.
// Keep this fast — never do slow work here.
static void onMqttMessage(char* topic, byte* payload, unsigned int length) {
    char msg[MQTT_MAX_PAYLOAD_LEN];
    unsigned int copyLen = length < sizeof(msg) - 1 ? length : sizeof(msg) - 1;
    memcpy(msg, payload, copyLen);
    msg[copyLen] = '\0';
    DBG("mqtt", "Inbound [%s]: %s", topic, msg);
    Serial.printf("[MQTT] Received [%s]: %s\n", topic, msg);
    // Extend here to route inbound commands if needed
}

// ─── Connection helper ────────────────────────────────────────────────────────
static bool mqttConnect() {
    DBG("mqtt", "Connecting to %s:%d as %s",
        MQTT_BROKER_HOST, MQTT_BROKER_PORT, MQTT_CLIENT_ID);

    s_client.setServer(MQTT_BROKER_HOST, MQTT_BROKER_PORT);
    s_client.setCallback(onMqttMessage);
    s_client.setKeepAlive(MQTT_KEEPALIVE_SEC);

    bool ok = s_client.connect(
        MQTT_CLIENT_ID,
        MQTT_USERNAME,
        MQTT_PASSWORD,
        MQTT_LWT_TOPIC,
        MQTT_LWT_QOS,
        MQTT_LWT_RETAIN,
        MQTT_LWT_MESSAGE
    );

    if (ok) {
        DBG("mqtt", "Connected");
        // Announce online
        s_client.publish(MQTT_TOPIC_STATUS, "online", true);
        Serial.printf("[MQTT] Connected to %s:%d\n",
                      MQTT_BROKER_HOST, MQTT_BROKER_PORT);
    } else {
        DBG("mqtt", "Failed, state=%d", s_client.state());
    }
    return ok;
}

// ─── Publish a single telemetry message ──────────────────────────────────────
static void publishTelemetry() {
    float t, h;
    bool  ok;
    {
        StateLock lock;
        t  = g_state.temperature;
        h  = g_state.humidity;
        ok = g_state.sensorOk;
    }

    if (!ok) {
        DBG("mqtt", "Skipping publish — sensor not ready");
        return;
    }

    char payload[MQTT_MAX_PAYLOAD_LEN];
    snprintf(payload, sizeof(payload),
             "{\"temp\":%.2f,\"hum\":%.2f,\"ts\":%lu}",
             t, h, (unsigned long)millis());

    DBG("mqtt", "Publishing telemetry: %s", payload);

    // Publish individual topics for dashboards that subscribe per-metric
    char tempStr[12], humStr[12];
    snprintf(tempStr, sizeof(tempStr), "%.2f", t);
    snprintf(humStr,  sizeof(humStr),  "%.2f", h);

    s_client.publish(MQTT_TOPIC_TEMP,      tempStr);
    s_client.publish(MQTT_TOPIC_HUMIDITY,  humStr);
    s_client.publish(MQTT_TOPIC_TELEMETRY, payload);

    Serial.printf("[MQTT] Published T=%.1f C  H=%.1f %%\n", t, h);
}

// ─── Main task ───────────────────────────────────────────────────────────────
static void taskMqttRun(void* /*pv*/) {
    DBG("mqtt", "Task started");

    uint32_t backoffMs     = MQTT_RECONNECT_DELAY_MS;
    uint32_t lastPublishMs = 0;

    // Wait for WiFi before doing anything
    DBG("mqtt", "Waiting for WiFi...");
    {
        bool wifi = false;
        while (!wifi) {
            { StateLock lock; wifi = g_state.wifiConnected; }
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    DBG("mqtt", "WiFi ready — connecting to broker");

    for (;;) {
        // ── Ensure connected ──────────────────────────────────────────────────
        if (!s_client.connected()) {
            { StateLock lock; g_state.mqttConnected = false; }

            bool wifi;
            { StateLock lock; wifi = g_state.wifiConnected; }

            if (!wifi) {
                DBG("mqtt", "No WiFi — waiting before retry");
                vTaskDelay(pdMS_TO_TICKS(backoffMs));
                continue;
            }

            if (mqttConnect()) {
                backoffMs = MQTT_RECONNECT_DELAY_MS; // reset on success
                { StateLock lock; g_state.mqttConnected = true; }
            } else {
                pushError(ErrorSeverity::WARNING, "MQTT connect failed");
                DBG("mqtt", "Retry in %lu ms", (unsigned long)backoffMs);
                vTaskDelay(pdMS_TO_TICKS(backoffMs));
                // Exponential back-off capped at max
                backoffMs = min(backoffMs * 2, (uint32_t)MQTT_RECONNECT_MAX_BACKOFF);
                continue;
            }
        }

        // ── Drain on-demand publish queue ─────────────────────────────────────
        MqttMessage qMsg;
        while (xQueueReceive(g_mqttPubQueue, &qMsg, 0) == pdTRUE) {
            DBG("mqtt", "Queue publish [%s]: %s", qMsg.topic, qMsg.payload);
            s_client.publish(qMsg.topic, qMsg.payload, qMsg.retain);
        }

        // ── Periodic telemetry publish ─────────────────────────────────────────
        uint32_t interval;
        bool     enabled;
        {
            StateLock lock;
            interval = g_state.mqttIntervalMs;
            enabled  = g_state.mqttEnabled;
        }

        if (enabled && (millis() - lastPublishMs >= interval)) {
            publishTelemetry();
            lastPublishMs = millis();
        }

        // ── Keepalive ─────────────────────────────────────────────────────────
        s_client.loop();

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void taskMqttStart() {
    xTaskCreatePinnedToCore(
        taskMqttRun, "MqttTask",
        STACK_MQTT, nullptr,
        PRI_MQTT, nullptr,
        CORE_COMMS
    );
}

void taskMqttSelfTest() {
    Serial.printf("[TEST][MQTT ] Broker: %s:%d  Client: %s\n",
                  MQTT_BROKER_HOST, MQTT_BROKER_PORT, MQTT_CLIENT_ID);
    Serial.printf("[TEST][MQTT ] Base topic: %s\n",   MQTT_TOPIC_BASE);
    Serial.printf("[TEST][MQTT ] Publish interval: %d ms\n", MQTT_PUBLISH_INTERVAL_MS);

    // Queue enqueue/dequeue round-trip
    bool sent = mqttEnqueue("test/topic", "test_payload");
    MqttMessage readback;
    bool recv  = (xQueueReceive(g_mqttPubQueue, &readback, pdMS_TO_TICKS(100)) == pdTRUE);
    bool match = recv && strcmp(readback.topic, "test/topic") == 0
                      && strcmp(readback.payload, "test_payload") == 0;
    Serial.printf("[TEST][MQTT ] Pub queue round-trip: %s\n",
                  match ? "PASS" : "FAIL");
    DBG("mqtt", "Self-test: %s", match ? "PASS" : "FAIL");
}
