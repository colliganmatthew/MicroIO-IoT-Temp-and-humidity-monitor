#include "task_wifi.h"
#include "../shared_state.h"
#include "../config.h"
#include "../debug.h"
#include <Arduino.h>
#include <WiFi.h>

// =============================================================================
// task_wifi.cpp  —  WiFi connection manager
//
// Connects on startup, monitors connection every WIFI_RECONNECT_MS ms,
// and reconnects automatically if the link is lost.
// Updates g_state.wifiConnected under mutex so all other tasks can read it.
// =============================================================================

static void taskWifiManager(void* /*pv*/) {
    DBG("wifi", "Task started — connecting to \"%s\"", WIFI_SSID);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    for (;;) {
        bool connected = (WiFi.status() == WL_CONNECTED);

        // Update shared state
        { StateLock lock; g_state.wifiConnected = connected; }

        if (!connected) {
            DBG("wifi", "Not connected — attempting reconnect...");
            WiFi.disconnect(true);
            vTaskDelay(pdMS_TO_TICKS(500));
            WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

            // Wait up to 20 s for the connection to establish
            int attempts = 0;
            while (WiFi.status() != WL_CONNECTED && attempts < 40) {
                vTaskDelay(pdMS_TO_TICKS(500));
                attempts++;
            }

            if (WiFi.status() == WL_CONNECTED) {
                DBG("wifi", "Connected! IP: %s",
                    WiFi.localIP().toString().c_str());
                Serial.printf("[WiFi] Connected — IP: %s\n",
                              WiFi.localIP().toString().c_str());
                { StateLock lock; g_state.wifiConnected = true; }
            } else {
                DBG("wifi", "Connection failed after 20 s — will retry");
                pushError(ErrorSeverity::WARNING, "WiFi connect failed");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(WIFI_RECONNECT_MS));
    }
}

void taskWifiStart() {
    xTaskCreatePinnedToCore(
        taskWifiManager, "WiFiMgr",
        STACK_WIFI, nullptr,
        PRI_WIFI, nullptr,
        CORE_COMMS
    );
}

void taskWifiSelfTest() {
    DBG("wifi", "Self-test: SSID=\"%s\" reconnect_ms=%d",
        WIFI_SSID, WIFI_RECONNECT_MS);
    Serial.printf("[TEST][WiFi ] SSID: %s  Reconnect every: %d ms\n",
                  WIFI_SSID, WIFI_RECONNECT_MS);
}
