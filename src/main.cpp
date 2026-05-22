#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include "config.h"
#include "core/debug.h"
#include "core/shared_state.h"
#include "tasks/task_wifi.h"
#include "tasks/task_sensor.h"
#include "tasks/task_serial.h"
#include "tasks/task_scpi.h"
#include "tasks/task_mqtt.h"
#include "tasks/task_display.h"
#include "tasks/task_error.h"

// g_display is defined in task_display.cpp — extern so main can call begin()
extern Adafruit_SSD1306 g_display;

// =============================================================================
// setup()
//
// Order of operations:
//   1. Serial + I2C hardware initialisation
//   2. sharedStateInit()  — creates mutex + queues
//   3. Self-tests          — each subsystem validates itself (optional)
//   4. Start FreeRTOS tasks
//
// After setup() returns, loop() runs as the Arduino idle task.
// All real work is in the pinned tasks.
// =============================================================================
void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(300);

    Serial.println("\n========================================");
    Serial.println(" DHT11 Temperature & Humidity Monitor");
    Serial.println(" ESP32 + FreeRTOS + MQTT");
    Serial.println("========================================");
    Serial.printf(" Debug logging : %s\n",
                  DEBUG_ENABLED ? "ENABLED" : "DISABLED");
    Serial.printf(" MQTT broker   : %s:%d\n",
                  MQTT_BROKER_HOST, MQTT_BROKER_PORT);
    Serial.printf(" Sensor pin    : GPIO %d\n", DHT_PIN);
    Serial.printf(" Publish every : %d ms\n\n", MQTT_PUBLISH_INTERVAL_MS);

    DBG("main", "Serial started at %d baud", SERIAL_BAUD);

    Wire.begin();
    DBG("main", "I2C started");

    sharedStateInit();
    DBG("main", "Shared state initialised");

    // ── Self-tests ─────────────────────────────────────────────────────────────
#if RUN_SELF_TESTS
    Serial.println("--- Self-tests ---");
    taskDisplaySelfTest();  // also calls g_display.begin()
    taskSensorSelfTest();   // reads one sample to confirm wiring
    taskSerialSelfTest();
    taskScpiSelfTest();
    taskMqttSelfTest();
    taskErrorSelfTest();
    taskWifiSelfTest();
    Serial.println("--- Self-tests complete ---\n");
#else
    // Still need to initialise the display even without tests
    if (!g_display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
        Serial.println("[Main] WARNING: OLED not found");
        pushError(ErrorSeverity::WARNING, "OLED not found at startup");
    }
#endif

    // ── Start tasks ────────────────────────────────────────────────────────────
    //
    // Core 0 (comms + sensing):
    taskWifiStart();    // Pri 4 — connects WiFi, monitors link
    taskSerialStart();  // Pri 4 — reads UART, pushes lines to queue
    taskSensorStart();  // Pri 3 — DHT11 sampling loop
    taskScpiStart();    // Pri 3 — dispatches SCPI commands from queue
    taskMqttStart();    // Pri 2 — broker connection + telemetry publish
    //
    // Core 1 (display + error handling):
    taskDisplayStart(); // Pri 2 — OLED refresh loop
    taskErrorStart();   // Pri 1 — error queue consumer + heap watchdog

    DBG("main", "All tasks started");
    Serial.println("[Main] All tasks running.");
    Serial.println("[Main] Send SCPI commands over serial. Try: SYST:HELP?\n");
}

void loop() {
    // All work is in pinned tasks — loop() just keeps the idle task fed.
    vTaskDelay(pdMS_TO_TICKS(1000));
}
