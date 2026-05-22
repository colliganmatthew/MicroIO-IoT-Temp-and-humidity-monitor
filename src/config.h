#pragma once

// =============================================================================
// NAMING CONVENTIONS
// =============================================================================
//
// Prefixes
// ────────
//  g_   Global   — shared across multiple files via extern
//                  e.g. g_state, g_stateMutex, g_serialQueue
//
//  s_   Static   — private to a single .cpp file, nothing outside can see it
//                  e.g. s_dht, s_buf, s_client
//
//  p_   Pointer  — holds a memory address rather than a value directly
//                  e.g. p_display, p_sensor
//
//  m_   Member   — belongs to a class or struct instance
//                  e.g. m_temperature, m_pin
//
//  k_   Constant — compile-time value that never changes
//                  e.g. kMaxRetries, kBufferSize
//



// =============================================================================
// config.h  —  ALL project tunables live here.
//
// Sections:
//   1. Debug
//   2. Hardware pins
//   3. WiFi
//   4. MQTT
//   5. Sensor
//   6. Display
//   7. SCPI / Serial
//   8. Task parameters
// =============================================================================


// ─── 1. DEBUG ─────────────────────────────────────────────────────────────────
//
// Set DEBUG_ENABLED to 1 to print verbose logs from every function.
// Set to 0 for production builds — zero runtime overhead, no Serial.printf
// calls are compiled in.
//
#define DEBUG_ENABLED   1


// ─── 2. HARDWARE PINS ────────────────────────────────────────────────────────
// DHT PINS
#define DHT_PIN         4       // GPIO pin connected to DHT11 DATA line
#define DHT_TYPE        DHT11   // DHT11 or DHT22

// PIR MOTION (HC-SR501) PINS
#define PIR_PIN         5

// OLED is on the default I2C bus (GPIO 21 = SDA, GPIO 22 = SCL)
#define OLED_ADDRESS    0x3C
#define OLED_WIDTH      128
#define OLED_HEIGHT     64
#define OLED_RESET_PIN  -1      // -1 = share Arduino reset pin


// ─── 3. WIFI ─────────────────────────────────────────────────────────────────
//
#define WIFI_SSID "TP-Link_8080"
#define WIFI_PASSWORD "70203455"
#define WIFI_RECONNECT_MS   5000   // attempt reconnect every 10 s when lost


// ─── 4. MQTT ─────────────────────────────────────────────────────────────────
//
#define MQTT_BROKER_HOST        "192.168.1.243"
#define MQTT_BROKER_PORT        1883
#define MQTT_USERNAME           ""
#define MQTT_PASSWORD           ""
#define MQTT_CLIENT_ID          "esp32-dht11-monitor"

// Topic base — all sensor topics hang off this prefix
#define MQTT_TOPIC_BASE         "sensors/dht11"

// Individual publish topics
#define MQTT_TOPIC_TEMP         MQTT_TOPIC_BASE "/temperature"
#define MQTT_TOPIC_HUMIDITY     MQTT_TOPIC_BASE "/humidity"
#define MQTT_TOPIC_TELEMETRY    MQTT_TOPIC_BASE "/telemetry"   // combined JSON
#define MQTT_TOPIC_STATUS       MQTT_TOPIC_BASE "/status"

// Last-will — broker publishes this if the device drops unexpectedly
#define MQTT_LWT_TOPIC          MQTT_TOPIC_STATUS
#define MQTT_LWT_MESSAGE        "offline"
#define MQTT_LWT_QOS            1
#define MQTT_LWT_RETAIN         true

// How often the MQTT task publishes sensor data (milliseconds)
// Can be overridden at runtime via SCPI command MQTT:INTV <ms>
#define MQTT_PUBLISH_INTERVAL_MS    30000   // 30 seconds default

// Queue depths for the internal MQTT message queues
#define MQTT_PUBLISH_QUEUE_DEPTH    16
#define MQTT_MAX_TOPIC_LEN          128
#define MQTT_MAX_PAYLOAD_LEN        256

// Back-off limits for reconnection
#define MQTT_RECONNECT_DELAY_MS     5000
#define MQTT_RECONNECT_MAX_BACKOFF  60000
#define MQTT_KEEPALIVE_SEC          60


// ─── 5. SENSOR ───────────────────────────────────────────────────────────────
//
// How often the DHT task takes a reading (milliseconds).
// DHT11 minimum reliable sample interval is 2000 ms.
// Can be overridden at runtime via SCPI command SENS:RATE <ms>
#define DHT_SAMPLE_INTERVAL_MS   5000

// PIR Motion Sensor
#define PIR_SAMPLE_INTERVAL_MS   200
#define PIR_WARMUP_MS            30000 //HC-SR501 requires a 30 second warm up period to stabalise


// Threshold alerts — push an error event when exceeded
#define TEMP_WARN_HIGH_C        35.0f   // warn above this temperature
#define TEMP_WARN_LOW_C         0.0f    // warn below this temperature
#define HUMIDITY_WARN_HIGH_PCT  90.0f   // warn above this humidity
#define HUMIDITY_WARN_LOW_PCT   10.0f   // warn below this humidity


// ─── 6. DISPLAY ──────────────────────────────────────────────────────────────
//
#define DISPLAY_REFRESH_MS  500     // OLED re-render interval
#define ERROR_CHECK_MS      1000    // error task queue timeout + health check interval


// ─── 7. SCPI / SERIAL ────────────────────────────────────────────────────────
//
#define SERIAL_BAUD         115200
#define SERIAL_LINE_MAX     128     // max characters per SCPI command line
#define SERIAL_QUEUE_DEPTH  8       // slots in the serial → SCPI queue


// ─── 8. TASK PARAMETERS ──────────────────────────────────────────────────────
//
// Core assignments
#define CORE_COMMS      0   // WiFi, MQTT, serial, SCPI, sensor
#define CORE_APP        1   // display, error handler

// Stack sizes (bytes) — increase if you hit stack-overflow panics
#define STACK_WIFI      4096
#define STACK_SENSOR    4096
#define STACK_SERIAL    2048
#define STACK_SCPI      4096
#define STACK_MQTT      8192    // HTTP + JSON parsing needs headroom
#define STACK_DISPLAY   4096
#define STACK_ERROR     2048

// Task priorities  (higher number = higher urgency)
#define PRI_WIFI        4
#define PRI_SERIAL      4
#define PRI_SENSOR      3
#define PRI_SCPI        3
#define PRI_MQTT        2
#define PRI_DISPLAY     2
#define PRI_ERROR       1

// Self-test flag — set 0 to skip startup tests
#define RUN_SELF_TESTS  1

// ─── 9. MEMORY THRESHOLDS ────────────────────────────────────────────────────
//
#define HEAP_WARN_BYTES     20480   // warn below 20 KB free heap
#define HEAP_CRIT_BYTES      8192   // critical below 8 KB
