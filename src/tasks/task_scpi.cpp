#include "task_scpi.h"
#include "../core/shared_state.h"
#include "../scpi/scpi_commands.h"
#include "../config.h"
#include "../core/debug.h"
#include <Arduino.h>
#include <string.h>

// =============================================================================
// task_scpi.cpp  —  generic SCPI dispatcher task
//
// Blocks on g_serialQueue. Each line received is passed to scpiDispatch()
// which walks g_scpiRegistry[]. This task knows nothing about individual
// commands — all logic lives in scpi_commands.cpp.
// =============================================================================

static void taskScpiHandler(void* /*pv*/) {
    char line[SERIAL_LINE_MAX];

    DBG("scpi", "Task started — %u commands registered",
        (unsigned)scpiCommandCount());
    Serial.printf("[SCPI] Ready — %u commands. Try: SYST:HELP?\n",
                  (unsigned)scpiCommandCount());

    for (;;) {
        if (xQueueReceive(g_serialQueue, line, portMAX_DELAY) == pdTRUE) {
            DBG("scpi", "Received from queue: \"%s\"", line);
            scpiDispatch(line);
        }
    }
}

void taskScpiStart() {
    xTaskCreatePinnedToCore(
        taskScpiHandler, "ScpiHandler",
        STACK_SCPI, nullptr,
        PRI_SCPI, nullptr,
        CORE_COMMS
    );
}

void taskScpiSelfTest() {
    Serial.println("[TEST][SCPI ] Running dispatch tests...");
    int pass = 0, fail = 0;

    auto check = [&](const char* label, bool ok) {
        Serial.printf("[TEST][SCPI ]   %-32s %s\n",
                      label, ok ? "PASS" : "FAIL");
        ok ? pass++ : fail++;
    };

    // *IDN? — just call and note response above
    Serial.print("[TEST][SCPI ]   *IDN?                            -> ");
    scpiDispatch("*IDN?");

    // Set sample rate
    scpiDispatch("SENS:RATE 4000");
    { uint32_t r; { StateLock lock; r = g_state.sampleIntervalMs; }
      check("SENS:RATE 4000 -> sampleIntervalMs==4000", r == 4000); }

    // MQTT enable toggle
    scpiDispatch("MQTT:EN OFF");
    { bool e; { StateLock lock; e = g_state.mqttEnabled; }
      check("MQTT:EN OFF -> mqttEnabled==false", !e); }
    scpiDispatch("MQTT:EN ON");
    { bool e; { StateLock lock; e = g_state.mqttEnabled; }
      check("MQTT:EN ON  -> mqttEnabled==true", e); }

    // Alert thresholds
    scpiDispatch("ALRT:TEMP:HI 40.0");
    { float v; { StateLock lock; v = g_state.tempHighThresh; }
      check("ALRT:TEMP:HI 40.0", v > 39.9f && v < 40.1f); }

    // *RST resets everything
    scpiDispatch("SENS:RATE 8000");
    scpiDispatch("*RST");
    { uint32_t r; { StateLock lock; r = g_state.sampleIntervalMs; }
      check("*RST -> sampleIntervalMs restored", r == DHT_SAMPLE_INTERVAL_MS); }

    // Unknown command
    scpiDispatch("BOGUS:CMD?");
    { char e[64]; { StateLock lock; strncpy(e, g_state.lastScpiError, 64); }
      check("Unknown cmd -> lastScpiError=-113", strstr(e, "-113") != nullptr); }

    Serial.printf("[TEST][SCPI ] %d passed, %d failed\n\n", pass, fail);
    DBG("scpi", "Self-test: %d pass, %d fail", pass, fail);
}
