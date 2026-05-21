#include "task_wifi.h"
#include "../shared_state.h"
#include "../config.h"
#include "../debug.h"

#include <Arduino.h>
#include <WiFi.h>

static void taskWifiManager(void* /*pv*/) {

    DBG("wifi", "Connecting to \"%s\"", WIFI_SSID);

    WiFi.mode(WIFI_STA);

    // Start initial connection
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    // -----------------------------------------------------------------
    // INITIAL CONNECTION PHASE
    // -----------------------------------------------------------------

    while (WiFi.status() != WL_CONNECTED) {

        DBG("wifi", "Connecting... status=%d", WiFi.status());

        vTaskDelay(pdMS_TO_TICKS(500));
    }

    DBG("wifi", "Connected! IP: %s",
        WiFi.localIP().toString().c_str());

    Serial.printf("[WiFi] Connected — IP: %s\n",
                  WiFi.localIP().toString().c_str());

    {
        StateLock lock;
        g_state.wifiConnected = true;
    }

    // -----------------------------------------------------------------
    // NORMAL MONITORING LOOP
    // -----------------------------------------------------------------

    for (;;) {

        bool connected = (WiFi.status() == WL_CONNECTED);

        {
            StateLock lock;
            g_state.wifiConnected = connected;
        }

        if (!connected) {

            DBG("wifi", "WiFi lost — reconnecting...");

            WiFi.reconnect();

            int attempts = 0;

            while (WiFi.status() != WL_CONNECTED && attempts < 40) {

                DBG("wifi", "Reconnect status=%d", WiFi.status());

                vTaskDelay(pdMS_TO_TICKS(500));
                attempts++;
            }

            if (WiFi.status() == WL_CONNECTED) {

                DBG("wifi", "Reconnected! IP: %s",
                    WiFi.localIP().toString().c_str());

                {
                    StateLock lock;
                    g_state.wifiConnected = true;
                }

            } else {

                DBG("wifi", "Reconnect failed");

                {
                    StateLock lock;
                    g_state.wifiConnected = false;
                }

                pushError(ErrorSeverity::WARNING,
                          "WiFi reconnect failed");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(WIFI_RECONNECT_MS));
    }
}

void taskWifiStart() {

    xTaskCreatePinnedToCore(
        taskWifiManager,
        "WiFiMgr",
        4096,
        nullptr,
        4,
        nullptr,
        0
    );
}

void taskWifiSelfTest() {

    DBG("wifi",
        "Self-test: SSID=\"%s\" reconnect_ms=%d",
        WIFI_SSID,
        WIFI_RECONNECT_MS);

    Serial.printf(
        "[TEST][WiFi ] SSID: %s  Reconnect every: %d ms\n",
        WIFI_SSID,
        WIFI_RECONNECT_MS
    );
}