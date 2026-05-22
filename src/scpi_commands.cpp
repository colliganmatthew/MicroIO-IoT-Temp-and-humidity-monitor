#include "scpi_commands.h"
#include "shared_state.h"
#include "debug.h"
#include <Arduino.h>
#include <string.h>
#include <stdlib.h>
#include "tasks/task_mqtt.h"

// =============================================================================
// scpi_commands.cpp  —  every SCPI command handler and the registry table
//
// ─── TO ADD A COMMAND ────────────────────────────────────────────────────────
//   1. Write a static handler:
//
//        static const char* handleMyCmd(const char* cmd) {
//            if (strchr(cmd, '?')) { ... return value; }
//            // parse param
//            const char* p = cmd + strlen("MY:CMD");
//            while (*p == ' ') p++;
//            float val = atof(p);
//            { StateLock lock; g_state.xxx = val; }
//            return nullptr;
//        }
//
//   2. Add to g_scpiRegistry[]:
//        { "MY:CMD", handleMyCmd, "Description for SYST:HELP?" },
//
// ─── RESPONSE CONVENTIONS ────────────────────────────────────────────────────
//   Query   → return value string
//   Set OK  → return nullptr  (no serial reply)
//   Error   → pushError() + return IEEE 488.2 error string
// =============================================================================

// Shared single-threaded response buffer (SCPI task processes one cmd at a time)
static char s_buf[128];

// ─── Forward declarations ─────────────────────────────────────────────────────
// Allows the registry table to appear before handler definitions below.
static const char* handleIdn        (const char*);
static const char* handleRst        (const char*);
static const char* handleMeasTemp   (const char*);
static const char* handleMeasHum    (const char*);
static const char* handleMeasAll    (const char*);
static const char* handleSensRate   (const char*);
static const char* handleMqttIntv   (const char*);
static const char* handleMqttEn     (const char*);
static const char* handleMqttPub    (const char*);
static const char* handleMqttStat   (const char*);
static const char* handleAlrtEn     (const char*);
static const char* handleAlrtTempHi (const char*);
static const char* handleAlrtTempLo (const char*);
static const char* handleAlrtHumHi  (const char*);
static const char* handleAlrtHumLo  (const char*);
static const char* handleSystErr    (const char*);
static const char* handleSystHelp   (const char*);
static const char* handleSystUptime (const char*);
static const char* handleSystHeap   (const char*);
static const char* handleWifiStat   (const char*);
static const char* handleTestAll    (const char*);



// =============================================================================
// REGISTRY  —  add new commands here only
// Terminated by sentinel { nullptr, nullptr, nullptr }
// =============================================================================
const ScpiCommand g_scpiRegistry[] = {
    { "*IDN",           handleIdn,       "Instrument identification"              },
    { "*RST",           handleRst,       "Reset all parameters to defaults"       },
    { "MEAS:TEMP",      handleMeasTemp,  "Query temperature (C)"                  },
    { "MEAS:HUM",       handleMeasHum,   "Query humidity (%)"                     },
    { "MEAS:ALL",       handleMeasAll,   "Query both as JSON"                     },
    { "SENS:RATE",      handleSensRate,  "Set/query sample interval (ms, min 2000)"},
    { "MQTT:INTV",      handleMqttIntv,  "Set/query MQTT publish interval (ms)"   },
    { "MQTT:EN",        handleMqttEn,    "Set/query MQTT publish enable: ON|OFF"  },
    { "MQTT:PUB",       handleMqttPub,   "Force immediate MQTT publish"           },
    { "MQTT:STAT",      handleMqttStat,  "Query MQTT connection: 1=connected"     },
    { "ALRT:EN",        handleAlrtEn,    "Set/query alert enable: ON|OFF"         },
    { "ALRT:TEMP:HI",   handleAlrtTempHi,"Set/query high temp alert threshold (C)"},
    { "ALRT:TEMP:LO",   handleAlrtTempLo,"Set/query low  temp alert threshold (C)"},
    { "ALRT:HUM:HI",    handleAlrtHumHi, "Set/query high humidity alert (%%)"    },
    { "ALRT:HUM:LO",    handleAlrtHumLo, "Set/query low  humidity alert (%%)"    },
    { "SYST:ERR",       handleSystErr,   "Query and clear last error"             },
    { "SYST:HELP",      handleSystHelp,  "List all commands"                      },
    { "SYST:UPTIME",    handleSystUptime,"Query uptime (ms)"                      },
    { "SYST:HEAP",      handleSystHeap,  "Query free heap (bytes)"                },
    { "WIFI:STAT",      handleWifiStat,  "Query WiFi connection: 1=connected"     },
    { "TEST:ALL",       handleTestAll,   "Run built-in self-test"                 },
    { nullptr,          nullptr,         nullptr                                  },
};



// ─── *IDN? ───────────────────────────────────────────────────────────────────
static const char* handleIdn(const char* /*cmd*/) {
    DBG("scpi", "*IDN? received");
    return "MyOrg,DHT11-Monitor,SN001,FW1.0";
}

// ─── *RST ────────────────────────────────────────────────────────────────────
// Resets all runtime-tunable parameters to config.h defaults.
static const char* handleRst(const char* /*cmd*/) {
    DBG("scpi", "*RST received");
    StateLock lock;
    g_state.sampleIntervalMs = SENSOR_SAMPLE_INTERVAL_MS;
    g_state.mqttIntervalMs   = MQTT_PUBLISH_INTERVAL_MS;
    g_state.mqttEnabled      = true;
    g_state.alertsEnabled    = true;
    g_state.tempHighThresh   = TEMP_WARN_HIGH_C;
    g_state.tempLowThresh    = TEMP_WARN_LOW_C;
    g_state.humHighThresh    = HUMIDITY_WARN_HIGH_PCT;
    g_state.humLowThresh     = HUMIDITY_WARN_LOW_PCT;
    strncpy(g_state.lastScpiError, "+0,\"No error\"",
            sizeof(g_state.lastScpiError));
    return nullptr;
}

// ─── MEAS:TEMP? ──────────────────────────────────────────────────────────────
// Returns the most recent temperature reading in Celsius.
static const char* handleMeasTemp(const char* /*cmd*/) {
    float t;
    bool  ok;
    { StateLock lock; t = g_state.temperature; ok = g_state.sensorOk; }
    if (!ok) {
        DBG("scpi", "MEAS:TEMP? — sensor not ready");
        return "-230,\"Data corrupt or stale\"";
    }
    snprintf(s_buf, sizeof(s_buf), "%.2f", t);
    DBG("scpi", "MEAS:TEMP? -> %s C", s_buf);
    return s_buf;
}

// ─── MEAS:HUM? ───────────────────────────────────────────────────────────────
// Returns the most recent relative humidity reading in percent.
static const char* handleMeasHum(const char* /*cmd*/) {
    float h;
    bool  ok;
    { StateLock lock; h = g_state.humidity; ok = g_state.sensorOk; }
    if (!ok) {
        DBG("scpi", "MEAS:HUM? — sensor not ready");
        return "-230,\"Data corrupt or stale\"";
    }
    snprintf(s_buf, sizeof(s_buf), "%.2f", h);
    DBG("scpi", "MEAS:HUM? -> %s %%", s_buf);
    return s_buf;
}

// ─── MEAS:ALL? ───────────────────────────────────────────────────────────────
// Returns both measurements as a JSON object.
static const char* handleMeasAll(const char* /*cmd*/) {
    float t, h;
    bool  ok;
    { StateLock lock; t = g_state.temperature; h = g_state.humidity; ok = g_state.sensorOk; }
    if (!ok) return "-230,\"Data corrupt or stale\"";
    snprintf(s_buf, sizeof(s_buf),
             "{\"temp\":%.2f,\"hum\":%.2f}", t, h);
    DBG("scpi", "MEAS:ALL? -> %s", s_buf);
    return s_buf;
}

// ─── SENS:RATE <ms> / SENS:RATE? ─────────────────────────────────────────────
// Sets or queries the sensor sample interval in milliseconds.
// Minimum 2000 ms for DHT11 reliability.
static const char* handleSensRate(const char* cmd) {
    if (strchr(cmd, '?')) {
        uint32_t r; { StateLock lock; r = g_state.sampleIntervalMs; }
        snprintf(s_buf, sizeof(s_buf), "%lu", (unsigned long)r);
        DBG("scpi", "SENS:RATE? -> %s ms", s_buf);
        return s_buf;
    }
    const char* p = cmd + 9; while (*p == ' ') p++;
    uint32_t ms = (uint32_t)atol(p);
    if (ms < 2000) {
        pushError(ErrorSeverity::WARNING, "SENS:RATE min 2000 ms for DHT11");
        return "-222,\"Data out of range (min 2000)\"";
    }
    { StateLock lock; g_state.sampleIntervalMs = ms; }
    DBG("scpi", "SENS:RATE set to %lu ms", (unsigned long)ms);
    return nullptr;
}

// ─── MQTT:INTV <ms> / MQTT:INTV? ─────────────────────────────────────────────
// Sets or queries the MQTT publish interval in milliseconds.
static const char* handleMqttIntv(const char* cmd) {
    if (strchr(cmd, '?')) {
        uint32_t v; { StateLock lock; v = g_state.mqttIntervalMs; }
        snprintf(s_buf, sizeof(s_buf), "%lu", (unsigned long)v);
        DBG("scpi", "MQTT:INTV? -> %s ms", s_buf);
        return s_buf;
    }
    const char* p = cmd + 9; while (*p == ' ') p++;
    uint32_t ms = (uint32_t)atol(p);
    if (ms < 1000) {
        pushError(ErrorSeverity::WARNING, "MQTT:INTV min 1000 ms");
        return "-222,\"Data out of range (min 1000)\"";
    }
    { StateLock lock; g_state.mqttIntervalMs = ms; }
    DBG("scpi", "MQTT:INTV set to %lu ms", (unsigned long)ms);
    return nullptr;
}

// ─── MQTT:EN ON|OFF / MQTT:EN? ───────────────────────────────────────────────
// Enables or disables periodic MQTT publishing.
static const char* handleMqttEn(const char* cmd) {
    if (strchr(cmd, '?')) {
        bool en; { StateLock lock; en = g_state.mqttEnabled; }
        DBG("scpi", "MQTT:EN? -> %d", en);
        return en ? "1" : "0";
    }
    const char* p = cmd + 7; while (*p == ' ') p++;
    bool en = (strncmp(p, "ON", 2) == 0 || p[0] == '1');
    { StateLock lock; g_state.mqttEnabled = en; }
    DBG("scpi", "MQTT:EN set to %s", en ? "ON" : "OFF");
    return nullptr;
}

// ─── MQTT:PUB ────────────────────────────────────────────────────────────────
// Forces an immediate MQTT telemetry publish regardless of interval timer.
static const char* handleMqttPub(const char* /*cmd*/) {
    DBG("scpi", "MQTT:PUB — forcing immediate publish");
    float t, h;
    bool  ok, conn;
    {
        StateLock lock;
        t    = g_state.temperature;
        h    = g_state.humidity;
        ok   = g_state.sensorOk;
        conn = g_state.mqttConnected;
    }
    if (!conn) return "-300,\"Device error: MQTT not connected\"";
    if (!ok)   return "-230,\"Data corrupt or stale\"";
    char payload[MQTT_MAX_PAYLOAD_LEN];
    snprintf(payload, sizeof(payload),
             "{\"temp\":%.2f,\"hum\":%.2f,\"ts\":%lu}",
             t, h, (unsigned long)millis());
    bool sent = mqttEnqueue(MQTT_TOPIC_TELEMETRY, payload);
    return sent ? nullptr : "-350,\"Queue overflow\"";
}

// ─── ALRT:EN ON|OFF / ALRT:EN? ───────────────────────────────────────────────
// Enables or disables threshold alerting.
static const char* handleAlrtEn(const char* cmd) {
    if (strchr(cmd, '?')) {
        bool en; { StateLock lock; en = g_state.alertsEnabled; }
        return en ? "1" : "0";
    }
    const char* p = cmd + 7; while (*p == ' ') p++;
    bool en = (strncmp(p, "ON", 2) == 0 || p[0] == '1');
    { StateLock lock; g_state.alertsEnabled = en; }
    DBG("scpi", "ALRT:EN set to %s", en ? "ON" : "OFF");
    return nullptr;
}

// ─── ALRT:TEMP:HI <val> / ALRT:TEMP:HI? ─────────────────────────────────────
static const char* handleAlrtTempHi(const char* cmd) {
    if (strchr(cmd, '?')) {
        float v; { StateLock lock; v = g_state.tempHighThresh; }
        snprintf(s_buf, sizeof(s_buf), "%.1f", v);
        return s_buf;
    }
    const char* p = cmd + 12; while (*p == ' ') p++;
    float val = atof(p);
    { StateLock lock; g_state.tempHighThresh = val; }
    DBG("scpi", "ALRT:TEMP:HI set to %.1f", val);
    return nullptr;
}

// ─── ALRT:TEMP:LO <val> / ALRT:TEMP:LO? ─────────────────────────────────────
static const char* handleAlrtTempLo(const char* cmd) {
    if (strchr(cmd, '?')) {
        float v; { StateLock lock; v = g_state.tempLowThresh; }
        snprintf(s_buf, sizeof(s_buf), "%.1f", v);
        return s_buf;
    }
    const char* p = cmd + 12; while (*p == ' ') p++;
    float val = atof(p);
    { StateLock lock; g_state.tempLowThresh = val; }
    DBG("scpi", "ALRT:TEMP:LO set to %.1f", val);
    return nullptr;
}

// ─── ALRT:HUM:HI <val> / ALRT:HUM:HI? ───────────────────────────────────────
static const char* handleAlrtHumHi(const char* cmd) {
    if (strchr(cmd, '?')) {
        float v; { StateLock lock; v = g_state.humHighThresh; }
        snprintf(s_buf, sizeof(s_buf), "%.1f", v);
        return s_buf;
    }
    const char* p = cmd + 11; while (*p == ' ') p++;
    float val = atof(p);
    { StateLock lock; g_state.humHighThresh = val; }
    DBG("scpi", "ALRT:HUM:HI set to %.1f", val);
    return nullptr;
}

// ─── ALRT:HUM:LO <val> / ALRT:HUM:LO? ───────────────────────────────────────
static const char* handleAlrtHumLo(const char* cmd) {
    if (strchr(cmd, '?')) {
        float v; { StateLock lock; v = g_state.humLowThresh; }
        snprintf(s_buf, sizeof(s_buf), "%.1f", v);
        return s_buf;
    }
    const char* p = cmd + 11; while (*p == ' ') p++;
    float val = atof(p);
    { StateLock lock; g_state.humLowThresh = val; }
    DBG("scpi", "ALRT:HUM:LO set to %.1f", val);
    return nullptr;
}

// ─── SYST:ERR? ───────────────────────────────────────────────────────────────
static const char* handleSystErr(const char* /*cmd*/) {
    StateLock lock;
    strncpy(s_buf, g_state.lastScpiError, sizeof(s_buf) - 1);
    strncpy(g_state.lastScpiError, "+0,\"No error\"",
            sizeof(g_state.lastScpiError));
    DBG("scpi", "SYST:ERR? -> %s", s_buf);
    return s_buf;
}

// ─── SYST:HELP? ──────────────────────────────────────────────────────────────
// Handled as a special case inside scpiDispatch() — no-op here.
static const char* handleSystHelp(const char* /*cmd*/) { return nullptr; }

// ─── SYST:UPTIME? ────────────────────────────────────────────────────────────
static const char* handleSystUptime(const char* /*cmd*/) {
    uint32_t t; { StateLock lock; t = g_state.uptimeMs; }
    snprintf(s_buf, sizeof(s_buf), "%lu", (unsigned long)t);
    return s_buf;
}

// ─── SYST:HEAP? ──────────────────────────────────────────────────────────────
static const char* handleSystHeap(const char* /*cmd*/) {
    uint32_t h; { StateLock lock; h = g_state.freeHeapBytes; }
    snprintf(s_buf, sizeof(s_buf), "%lu", (unsigned long)h);
    return s_buf;
}

// ─── WIFI:STAT? ──────────────────────────────────────────────────────────────
static const char* handleWifiStat(const char* /*cmd*/) {
    bool w; { StateLock lock; w = g_state.wifiConnected; }
    return w ? "1" : "0";
}

// ─── MQTT:STAT? ──────────────────────────────────────────────────────────────
static const char* handleMqttStat(const char* /*cmd*/) {
    bool m; { StateLock lock; m = g_state.mqttConnected; }
    return m ? "1" : "0";
}

// ─── TEST:ALL ────────────────────────────────────────────────────────────────
static const char* handleTestAll(const char* /*cmd*/) {
    bool pass = true;
    { StateLock lock; pass = g_state.sensorOk && g_state.systemOk; }
    DBG("scpi", "TEST:ALL -> %s", pass ? "PASS" : "FAIL");
    return pass ? "+0,\"Self-test passed\"" : "-330,\"Self-test failed\"";
}

// =============================================================================
// Dispatcher — do not edit; only edit the registry above
// =============================================================================

size_t scpiCommandCount() {
    size_t n = 0;
    while (g_scpiRegistry[n].command) n++;
    return n;
}

void scpiDispatch(const char* cmd) {
    if (!cmd || cmd[0] == '\0') return;
    DBG("scpi", "dispatch: \"%s\"", cmd);

    // Special case: SYST:HELP? prints the full registry
    if (strncmp(cmd, "SYST:HELP", 9) == 0) {
        Serial.println("Registered SCPI commands:");
        for (size_t i = 0; g_scpiRegistry[i].command; i++) {
            Serial.printf("  %-20s  %s\n",
                          g_scpiRegistry[i].command,
                          g_scpiRegistry[i].helpText);
        }
        return;
    }

    for (size_t i = 0; g_scpiRegistry[i].command; i++) {
        size_t len = strlen(g_scpiRegistry[i].command);
        if (strncmp(cmd, g_scpiRegistry[i].command, len) == 0) {
            char next = cmd[len];
            if (next == '\0' || next == ' ' || next == '?') {
                const char* resp = g_scpiRegistry[i].handler(cmd);
                if (resp) Serial.println(resp);
                return;
            }
        }
    }

    // No match
    Serial.println("-113,\"Undefined header\"");
    { StateLock lock;
      strncpy(g_state.lastScpiError, "-113,\"Undefined header\"",
              sizeof(g_state.lastScpiError)); }
    DBG("scpi", "unknown command: %s", cmd);
}
