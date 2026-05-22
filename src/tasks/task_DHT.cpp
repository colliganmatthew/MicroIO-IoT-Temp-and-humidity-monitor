#include "task_DHT.h"
#include "../core/shared_state.h"
#include "../config.h"
#include "../core/debug.h"
#include <Arduino.h>
#include <DHT.h>

// =============================================================================
// task_sensor.cpp  —  DHT11 sampling task
//
// Reads temperature and humidity every g_state.sampleIntervalMs ms
// (runtime-tunable via SCPI command SENS:RATE).
// Writes results into shared state and pushes threshold alerts to the
// error queue when alertsEnabled is true.
// =============================================================================

// DHT driver — declared here so only this translation unit touches the sensor
static DHT s_dht(DHT_PIN, DHT_TYPE);

static void taskDhtSensor(void* /*pv*/) {
    DBG("sensor", "Task started — GPIO %d, type DHT%d",
        DHT_PIN, (DHT_TYPE == DHT11 ? 11 : 22));

    s_dht.begin();

    // DHT11 needs at least 1 s after power-on before first read
    vTaskDelay(pdMS_TO_TICKS(1500));

    for (;;) {
        // Read sample interval (may have been changed by SCPI)
        uint32_t interval;
        bool     alertsOn;
        float    tHi, tLo, hHi, hLo;
        {
            StateLock lock;
            interval  = g_state.sampleIntervalMs;
            alertsOn  = g_state.alertsEnabled;
            tHi       = g_state.tempHighThresh;
            tLo       = g_state.tempLowThresh;
            hHi       = g_state.humHighThresh;
            hLo       = g_state.humLowThresh;
        }

        float temperature = s_dht.readTemperature(false); // false = Celsius
        float humidity    = s_dht.readHumidity();

        if (isnan(temperature) || isnan(humidity)) {
            // DHT11 occasionally returns NaN — log and skip this cycle
            DBG("sensor", "Read failed (NaN) — skipping cycle");
            pushError(ErrorSeverity::WARNING, "DHT11 read failed");
            {
                StateLock lock;
                g_state.DHT_Ok = false;
            }
        } else {
            DBG("sensor", "T=%.1f C  H=%.1f %%  interval=%lu ms",
                temperature, humidity, (unsigned long)interval);

            {
                StateLock lock;
                g_state.temperature  = temperature;
                g_state.humidity     = humidity;
                g_state.DHT_Ok     = true;
                g_state.lastSampleMs = millis();
            }

            // Threshold alerts
            if (alertsOn) {
                char msg[64];
                if (temperature > tHi) {
                    snprintf(msg, sizeof(msg),
                             "Temp high: %.1f C (thresh %.1f)", temperature, tHi);
                    pushError(ErrorSeverity::WARNING, msg);
                    DBG("sensor", "ALERT: %s", msg);
                }
                if (temperature < tLo) {
                    snprintf(msg, sizeof(msg),
                             "Temp low: %.1f C (thresh %.1f)", temperature, tLo);
                    pushError(ErrorSeverity::WARNING, msg);
                    DBG("sensor", "ALERT: %s", msg);
                }
                if (humidity > hHi) {
                    snprintf(msg, sizeof(msg),
                             "Humidity high: %.1f %% (thresh %.1f)", humidity, hHi);
                    pushError(ErrorSeverity::WARNING, msg);
                    DBG("sensor", "ALERT: %s", msg);
                }
                if (humidity < hLo) {
                    snprintf(msg, sizeof(msg),
                             "Humidity low: %.1f %% (thresh %.1f)", humidity, hLo);
                    pushError(ErrorSeverity::WARNING, msg);
                    DBG("sensor", "ALERT: %s", msg);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(interval));
    }
}

void taskDHTStart() {
    xTaskCreatePinnedToCore(
        taskDhtSensor, "DhtSensor",
        STACK_SENSOR, nullptr,
        PRI_SENSOR, nullptr,
        CORE_COMMS
    );
}

void taskDHTSelfTest() {
    DBG("DHT", "Self-test: init DHT on GPIO %d", DHT_PIN);
    Serial.printf("[TEST][DHT] DHT11 on GPIO %d  sample_ms=%d\n",
                  DHT_PIN, DHT_SAMPLE_INTERVAL_MS);

    s_dht.begin();
    vTaskDelay(pdMS_TO_TICKS(1500));

    float t = s_dht.readTemperature(false);
    float h = s_dht.readHumidity();

    if (isnan(t) || isnan(h)) {
        Serial.println("[TEST][DHT] Read: FAIL (NaN — check wiring)");
        pushError(ErrorSeverity::FAULT, "DHT11 self-test failed");
    } else {
        Serial.printf("[TEST][DHT] Read: PASS  T=%.1f C  H=%.1f %%\n", t, h);
    }
}
