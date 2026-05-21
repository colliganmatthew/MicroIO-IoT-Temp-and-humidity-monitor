#include "task_error.h"
#include "../shared_state.h"
#include "../config.h"
#include "../debug.h"
#include <Arduino.h>
#include <esp_heap_caps.h>

// =============================================================================
// task_error.cpp  —  error event consumer + system health watchdog
//
// Severity routing:
//   INFO     — serial log only
//   WARNING  — serial log + update SCPI error register
//   FAULT    — WARNING actions + set systemOk = false
//   CRITICAL — FAULT actions + disable MQTT + suspend (halt)
//
// Periodic health checks (every ERROR_CHECK_MS even when queue is idle):
//   - Update g_state.uptimeMs and g_state.freeHeapBytes
//   - Warn if heap drops below HEAP_WARN_BYTES
// =============================================================================

// Thresholds come from config.h (HEAP_WARN_BYTES, HEAP_CRIT_BYTES)

static const char* severityStr(ErrorSeverity s) {
    switch (s) {
        case ErrorSeverity::INFO:     return "INFO";
        case ErrorSeverity::WARNING:  return "WARN";
        case ErrorSeverity::FAULT:    return "FAULT";
        case ErrorSeverity::CRITICAL: return "CRIT";
        default:                      return "????";
    }
}

static void taskErrorHandler(void* /*pv*/) {
    DBG("error", "Task started");
    ErrorEvent ev;

    for (;;) {
        // Block up to ERROR_CHECK_MS waiting for an error event
        if (xQueueReceive(g_errorQueue, &ev,
                          pdMS_TO_TICKS(ERROR_CHECK_MS)) == pdTRUE) {

            Serial.printf("[ERROR][%s] %s\n", severityStr(ev.severity), ev.message);
            DBG("error", "[%s] %s", severityStr(ev.severity), ev.message);

            if (ev.severity >= ErrorSeverity::WARNING) {
                StateLock lock;
                strncpy(g_state.lastScpiError, ev.message,
                        sizeof(g_state.lastScpiError) - 1);
            }

            if (ev.severity >= ErrorSeverity::FAULT) {
                StateLock lock;
                g_state.systemOk = false;
            }

            if (ev.severity == ErrorSeverity::CRITICAL) {
                {
                    StateLock lock;
                    g_state.mqttEnabled   = false;
                }
                Serial.println("[ERROR] CRITICAL — system halted");
                DBG("error", "CRITICAL halt");
                vTaskSuspend(nullptr);
            }
        }

        // ── Periodic health update ────────────────────────────────────────────
        uint32_t heap = (uint32_t)esp_get_free_heap_size();
        {
            StateLock lock;
            g_state.uptimeMs      = millis();
            g_state.freeHeapBytes = heap;
        }

        // DBG("error", "Health: uptime=%lu ms  heap=%lu B",
        //     (unsigned long)millis(), (unsigned long)heap);

        if (heap < HEAP_CRIT_BYTES) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Heap critical: %lu B", (unsigned long)heap);
            pushError(ErrorSeverity::CRITICAL, msg);
        } else if (heap < HEAP_WARN_BYTES) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Heap low: %lu B", (unsigned long)heap);
            pushError(ErrorSeverity::WARNING, msg);
        }
    }
}

void taskErrorStart() {
    xTaskCreatePinnedToCore(
        taskErrorHandler, "ErrorHandler",
        STACK_ERROR, nullptr,
        PRI_ERROR, nullptr,
        CORE_APP
    );
}

void taskErrorSelfTest() {
    Serial.printf("[TEST][Error] Free heap: %lu B\n",
                  (unsigned long)esp_get_free_heap_size());

    pushError(ErrorSeverity::INFO,    "Self-test info event");
    pushError(ErrorSeverity::WARNING, "Self-test warning event");

    UBaseType_t depth = uxQueueMessagesWaiting(g_errorQueue);
    Serial.printf("[TEST][Error] Queue after 2 pushes: %u — %s\n",
                  (unsigned)depth, depth >= 2 ? "PASS" : "FAIL");

    // Drain test events
    ErrorEvent ev;
    while (xQueueReceive(g_errorQueue, &ev, 0) == pdTRUE) {}
    DBG("error", "Self-test done, queue drained");
}
