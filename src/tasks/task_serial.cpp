#include "task_serial.h"
#include "../shared_state.h"
#include "../config.h"
#include "../debug.h"
#include <Arduino.h>

// =============================================================================
// task_serial.cpp  —  UART reader
//
// Polls Serial every 5 ms, accumulates characters, and pushes complete lines
// onto g_serialQueue for the SCPI task to consume.
// Characters are upper-cased on ingestion for case-insensitive SCPI matching.
// =============================================================================

static void taskSerialComms(void* /*pv*/) {
    char line[SERIAL_LINE_MAX];
    int  pos = 0;

    DBG("serial", "Task started  baud=%d", SERIAL_BAUD);

    for (;;) {
        while (Serial.available()) {
            char c = (char)Serial.read();

            if (c == '\n' || c == '\r') {
                if (pos > 0) {
                    line[pos] = '\0';
                    DBG("serial", "Line ready: \"%s\"", line);
                    if (xQueueSend(g_serialQueue, line, 0) != pdTRUE) {
                        pushError(ErrorSeverity::WARNING,
                                  "Serial queue full — command dropped");
                        DBG("serial", "Queue full — dropped \"%s\"", line);
                    }
                    pos = 0;
                }
            } else if (pos < SERIAL_LINE_MAX - 1) {
                line[pos++] = (char)toupper((unsigned char)c);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void taskSerialStart() {
    xTaskCreatePinnedToCore(
        taskSerialComms, "SerialComms",
        STACK_SERIAL, nullptr,
        PRI_SERIAL, nullptr,
        CORE_COMMS
    );
}

void taskSerialSelfTest() {
    Serial.printf("[TEST][Serial] Baud=%d  LineMax=%d  QueueDepth=%d\n",
                  SERIAL_BAUD, SERIAL_LINE_MAX, SERIAL_QUEUE_DEPTH);

    const char* testLine = "*IDN?";
    char        readback[SERIAL_LINE_MAX];

    bool sent = (xQueueSend(g_serialQueue, testLine,
                            pdMS_TO_TICKS(100)) == pdTRUE);
    bool recv = sent && (xQueueReceive(g_serialQueue, readback,
                                      pdMS_TO_TICKS(100)) == pdTRUE);
    bool match = recv && (strcmp(readback, testLine) == 0);

    Serial.printf("[TEST][Serial] Queue round-trip: %s\n",
                  match ? "PASS" : "FAIL");
    DBG("serial", "Self-test result: %s", match ? "PASS" : "FAIL");
}
