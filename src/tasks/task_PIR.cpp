#include "task_PIR.h"
#include "../core/shared_state.h"
#include "../core/debug.h"
#include <Arduino.h>

static void taskPIRSensor(void* /*pv*/) {
    DBG("PIR", "Task Started - GPIO %d warmup %d ms",
    PIR_PIN, PIR_WARMUP_MS);

    pinMode(PIR_PIN, INPUT);

    //WARMUP PERIOD
    {
        StateLock lock; 
        g_state.pirReady = false;
    }
    vTaskDelay(pdMS_TO_TICKS(PIR_WARMUP_MS));
    {
        StateLock lock;
        g_state.pirReady = true;
    }
    DBG("PIR","Warmup complete - monitoring");

    //INF monitoring Loop
    for (;;) {
        bool motion = (digitalRead(PIR_PIN) == HIGH);

        {
            StateLock lock;
            g_state.motionDetected = motion;
            if (motion) g_state.lastMotionMs = millis();
        }

        if (motion) {
            DBG("PIR","Motion Detected!");
            pushError(ErrorSeverity::INFO,"Motion Detected!");
        }

        vTaskDelay(pdMS_TO_TICKS(PIR_SAMPLE_INTERVAL_MS));
    }
}

void taskPIRStart() {
    xTaskCreatePinnedToCore(
        taskPIRSensor, "PIRSensor",
        2048, nullptr,
        PRI_SENSOR, nullptr,
        CORE_COMMS
    );
}

void taskPIRSelfTest() {
    Serial.printf("[TEST][PIR ] GPIO %d warmup %d ms\n",
    PIR_PIN, PIR_WARMUP_MS);
    pinMode(PIR_PIN,INPUT);
    int raw = digitalRead(PIR_PIN);
    Serial.printf("[TEST][PIR ] Pin read: %d (0=no motion, 1=motion)\n", raw);
    DBG("PIR", "Self-Test raw pin = %d", raw);
}