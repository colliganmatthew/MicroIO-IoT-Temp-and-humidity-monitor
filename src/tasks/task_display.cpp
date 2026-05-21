#include "task_display.h"
#include "../shared_state.h"
#include "../config.h"
#include "../debug.h"
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// =============================================================================
// task_display.cpp  —  SSD1306 OLED refresh task
//
// Snapshots shared state under a short mutex lock, then performs all I2C
// rendering outside the lock so other tasks are not blocked by slow I2C.
//
// Screen layout (128x64):
//  Row 0 (y= 0):  "DHT11 Monitor"  [status dot]
//  Row 1 (y=12):  divider line
//  Row 2 (y=16):  Temp: XX.X C
//  Row 3 (y=28):  Hum:  XX.X %
//  Row 4 (y=40):  WiFi:[OK|--]  MQTT:[OK|--]
//  Row 5 (y=52):  Heap: XXXXXX B  or fault banner
// =============================================================================

Adafruit_SSD1306 g_display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET_PIN);

static void taskOledDisplay(void* /*pv*/) {
    DBG("display", "Task started — %dx%d @ 0x%02X",
        OLED_WIDTH, OLED_HEIGHT, OLED_ADDRESS);

    for (;;) {
        // ── Snapshot state (lock held for microseconds) ───────────────────────
        float    temperature, humidity;
        bool     sensorOk, wifiOk, mqttOk, sysOk;
        uint32_t heap;
        uint32_t sampleAgeMs;
        {
            StateLock lock;
            temperature  = g_state.temperature;
            humidity     = g_state.humidity;
            sensorOk     = g_state.sensorOk;
            wifiOk       = g_state.wifiConnected;
            mqttOk       = g_state.mqttConnected;
            sysOk        = g_state.systemOk;
            heap         = g_state.freeHeapBytes;
            sampleAgeMs  = millis() - g_state.lastSampleMs;
        }

        bool stale = sensorOk && (sampleAgeMs > 30000); // warn if >30 s old

        // ── Render ────────────────────────────────────────────────────────────
        g_display.clearDisplay();
        g_display.setTextSize(1);
        g_display.setTextColor(SSD1306_WHITE);

        // Row 0: title + system status indicator
        g_display.setCursor(0, 0);
        g_display.print("DHT11 Monitor");
        // Small status dot — right-aligned
        g_display.setCursor(116, 0);
        g_display.print(sysOk ? "*" : "!");

        // Divider
        g_display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

        // Row 2: temperature
        g_display.setCursor(0, 14);
        if (sensorOk) {
            g_display.printf("Temp: %5.1f C%s", temperature, stale ? "?" : " ");
        } else {
            g_display.print("Temp: --- C");
        }

        // Row 3: humidity
        g_display.setCursor(0, 26);
        if (sensorOk) {
            g_display.printf("Hum:  %5.1f %%%s", humidity, stale ? "?" : " ");
        } else {
            g_display.print("Hum:  --- %");
        }

        // Row 4: connectivity status
        g_display.setCursor(0, 38);
        g_display.printf("WiFi:%-2s  MQTT:%-2s",
                         wifiOk ? "OK" : "--",
                         mqttOk ? "OK" : "--");

        // Row 5: heap or fault banner
        g_display.setCursor(0, 50);
        if (!sysOk) {
            g_display.print("!!! SYSTEM FAULT !!!");
        } else {
            g_display.printf("Heap: %5lu B", (unsigned long)heap);
        }

        g_display.display();

        // DBG("display", "Rendered T=%.1f H=%.1f wifi=%d mqtt=%d",
        //     temperature, humidity, wifiOk, mqttOk);

        vTaskDelay(pdMS_TO_TICKS(DISPLAY_REFRESH_MS));
    }
}

void taskDisplayStart() {
    xTaskCreatePinnedToCore(
        taskOledDisplay, "OledDisplay",
        STACK_DISPLAY, nullptr,
        PRI_DISPLAY, nullptr,
        CORE_APP
    );
}

void taskDisplaySelfTest() {
    Serial.printf("[TEST][Disp ] OLED 0x%02X  %dx%d\n",
                  OLED_ADDRESS, OLED_WIDTH, OLED_HEIGHT);

    if (g_display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
        Serial.println("[TEST][Disp ] Init: PASS");
        g_display.clearDisplay();
        g_display.setTextSize(1);
        g_display.setTextColor(SSD1306_WHITE);
        g_display.setCursor(0,  0); g_display.println("DHT11 Monitor");
        g_display.setCursor(0, 14); g_display.println("Self-test OK");
        g_display.setCursor(0, 28); g_display.println("Starting...");
        g_display.display();
        delay(1500);
        g_display.clearDisplay();
        g_display.display();
        DBG("display", "Splash rendered OK");
    } else {
        Serial.println("[TEST][Disp ] Init: FAIL — check wiring + OLED_ADDRESS");
        pushError(ErrorSeverity::FAULT, "OLED init failed");
        DBG("display", "Init FAILED");
    }
}
