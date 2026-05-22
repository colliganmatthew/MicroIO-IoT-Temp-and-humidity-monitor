#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include <stdint.h>
#include <string.h>
#include "config.h"

// =============================================================================
// shared_state.h  —  central data shared between all tasks
//
// Rules:
//   1. Always access SensorState fields via a StateLock scope.
//   2. Keep the lock only long enough to copy values in/out.
//   3. Never take the lock from an ISR — use pushError() instead.
// =============================================================================


// ─── Error severity ───────────────────────────────────────────────────────────

enum class ErrorSeverity : uint8_t {
    INFO     = 0,
    WARNING  = 1,
    FAULT    = 2,
    CRITICAL = 3,
};

struct ErrorEvent {
    ErrorSeverity severity;
    char          message[64];
};


// ─── Main shared state ────────────────────────────────────────────────────────

struct SensorState {

    // ── Live sensor data ──────────────────────────────────────────────────────
    float    temperature    = 0.0f;     // degrees Celsius
    float    humidity       = 0.0f;     // percent relative humidity
    bool     sensorOk       = false;    // true = last read was valid
    uint32_t lastSampleMs   = 0;        // millis() of last successful read

    // ── Runtime-tunable parameters (changeable via SCPI) ──────────────────────
    uint32_t sampleIntervalMs   = SENSOR_SAMPLE_INTERVAL_MS;
    uint32_t mqttIntervalMs     = MQTT_PUBLISH_INTERVAL_MS;
    bool     mqttEnabled        = true;     // MQTT:EN ON|OFF
    bool     alertsEnabled      = true;     // ALRT:EN ON|OFF
    float    tempHighThresh     = TEMP_WARN_HIGH_C;
    float    tempLowThresh      = TEMP_WARN_LOW_C;
    float    humHighThresh      = HUMIDITY_WARN_HIGH_PCT;
    float    humLowThresh       = HUMIDITY_WARN_LOW_PCT;

    // ── Connectivity ──────────────────────────────────────────────────────────
    bool     wifiConnected  = false;
    bool     mqttConnected  = false;

    // ── System health ─────────────────────────────────────────────────────────
    bool     systemOk       = true;
    uint32_t uptimeMs       = 0;
    uint32_t freeHeapBytes  = 0;

    // ── SCPI error register (IEEE 488.2) ──────────────────────────────────────
    char     lastScpiError[64] = "+0,\"No error\"";
};


// ─── Globals (defined in shared_state.cpp) ────────────────────────────────────

extern SensorState       g_state;
extern SemaphoreHandle_t g_stateMutex;
extern QueueHandle_t     g_serialQueue;   // assembled lines  → SCPI task
extern QueueHandle_t     g_errorQueue;    // ErrorEvent items → error task
extern QueueHandle_t     g_mqttPubQueue;  // MqttMessage items → MQTT task

void sharedStateInit();


// ─── RAII mutex guard ─────────────────────────────────────────────────────────
//
// Usage:
//   {
//       StateLock lock;
//       g_state.temperature = reading;
//   }  // released automatically
//
struct StateLock {
    StateLock()  { xSemaphoreTake(g_stateMutex, portMAX_DELAY); }
    ~StateLock() { xSemaphoreGive(g_stateMutex); }
};


// ─── Error helper ─────────────────────────────────────────────────────────────
//
// Non-blocking push — drops silently if the error queue is full.
// Safe to call from any task (not from ISRs).
//
inline void pushError(ErrorSeverity sev, const char* msg) {
    ErrorEvent ev;
    ev.severity = sev;
    strncpy(ev.message, msg, sizeof(ev.message) - 1);
    ev.message[sizeof(ev.message) - 1] = '\0';
    xQueueSend(g_errorQueue, &ev, 0);
}
