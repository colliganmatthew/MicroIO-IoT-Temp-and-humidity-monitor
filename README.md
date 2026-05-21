# DHT11 Temperature & Humidity Monitor

ESP32 · FreeRTOS · MQTT · PlatformIO

Monitors temperature and humidity with a DHT11 sensor, displays readings on an
SSD1306 OLED, publishes to MQTT on a configurable interval, and exposes full
runtime control via SCPI commands over serial.

---

## Hardware

| Component | Connection |
|---|---|
| DHT11 | DATA → GPIO 4 (change `DHT_PIN` in config.h) |
| SSD1306 OLED | SDA → GPIO 21, SCL → GPIO 22 (default I2C) |

---

## Quick start

1. Copy `src/config.h` and fill in your WiFi and MQTT credentials.
2. Flash with PlatformIO: `pio run --target upload`
3. Open the serial monitor at 115200 baud.
4. Type `SYST:HELP?` and press Enter to list all SCPI commands.

---

## Debug logging

Controlled by a single flag in `config.h`:

```cpp
#define DEBUG_ENABLED   1   // 1 = verbose, 0 = silent (production)
```

When `DEBUG_ENABLED = 1`, every function across every task prints to serial:

```
[DBG][sensor  ] T=23.4 C  H=55.0 %  interval=5000 ms
[DBG][mqtt    ] Publishing telemetry: {"temp":23.40,"hum":55.00,"ts":12345}
[DBG][scpi    ] dispatch: "MEAS:TEMP?"
```

When `DEBUG_ENABLED = 0`, all `DBG()` calls compile away entirely — zero
Flash, zero RAM, zero runtime cost.

---

## SCPI commands

Connect a serial terminal at 115200 baud and type commands followed by Enter.
Commands are case-insensitive.

| Command | Description |
|---|---|
| `*IDN?` | Instrument identification |
| `*RST` | Reset all parameters to defaults |
| `MEAS:TEMP?` | Query latest temperature (°C) |
| `MEAS:HUM?` | Query latest humidity (%) |
| `MEAS:ALL?` | Query both as JSON |
| `SENS:RATE <ms>` | Set sample interval (min 2000 ms) |
| `SENS:RATE?` | Query sample interval |
| `MQTT:INTV <ms>` | Set MQTT publish interval |
| `MQTT:INTV?` | Query MQTT publish interval |
| `MQTT:EN ON\|OFF` | Enable/disable periodic MQTT publishing |
| `MQTT:EN?` | Query MQTT enable state |
| `MQTT:PUB` | Force immediate MQTT publish |
| `MQTT:STAT?` | Query MQTT connection (1=connected) |
| `ALRT:EN ON\|OFF` | Enable/disable threshold alerts |
| `ALRT:TEMP:HI <val>` | Set high temperature alert threshold (°C) |
| `ALRT:TEMP:LO <val>` | Set low temperature alert threshold (°C) |
| `ALRT:HUM:HI <val>` | Set high humidity alert threshold (%) |
| `ALRT:HUM:LO <val>` | Set low humidity alert threshold (%) |
| `SYST:ERR?` | Query and clear last error |
| `SYST:HELP?` | List all commands |
| `SYST:UPTIME?` | Query uptime (ms) |
| `SYST:HEAP?` | Query free heap (bytes) |
| `WIFI:STAT?` | Query WiFi connection (1=connected) |
| `TEST:ALL` | Run built-in self-test |

---

## MQTT topics

All topics are prefixed with `MQTT_TOPIC_BASE` (default `sensors/dht11`).

| Topic | Content |
|---|---|
| `sensors/dht11/temperature` | Temperature float, e.g. `23.40` |
| `sensors/dht11/humidity` | Humidity float, e.g. `55.00` |
| `sensors/dht11/telemetry` | JSON: `{"temp":23.40,"hum":55.00,"ts":12345}` |
| `sensors/dht11/status` | `online` / `offline` (LWT) |

---

## Task architecture

| Task | Core | Priority | Role |
|---|---|---|---|
| WiFi manager | 0 | 4 | Connect + reconnect WiFi |
| Serial comms | 0 | 4 | Read UART → push to queue |
| DHT sensor | 0 | 3 | Sample DHT11, threshold alerts |
| SCPI handler | 0 | 3 | Dispatch commands from queue |
| MQTT | 0 | 2 | Broker connection + publish |
| OLED display | 1 | 2 | 500 ms screen refresh |
| Error handler | 1 | 1 | Event queue + heap watchdog |

---

## Adding a SCPI command

Open `src/scpi_commands.cpp`:

1. Write a handler function:
```cpp
static const char* handleMyCmd(const char* cmd) {
    if (strchr(cmd, '?')) {
        float v; { StateLock lock; v = g_state.myValue; }
        snprintf(s_buf, sizeof(s_buf), "%.2f", v);
        return s_buf;
    }
    const char* p = cmd + strlen("MY:CMD"); while (*p == ' ') p++;
    float val = atof(p);
    { StateLock lock; g_state.myValue = val; }
    DBG("scpi", "MY:CMD set to %.2f", val);
    return nullptr;
}
```

2. Add one row to `g_scpiRegistry[]`:
```cpp
{ "MY:CMD", handleMyCmd, "Description shown by SYST:HELP?" },
```

No other files change.

---

## Configuration reference (`src/config.h`)

| Define | Default | Description |
|---|---|---|
| `DEBUG_ENABLED` | `1` | Verbose debug logging (0 = off) |
| `DHT_PIN` | `4` | GPIO for DHT11 data line |
| `WIFI_SSID` | — | Your WiFi network name |
| `WIFI_PASSWORD` | — | Your WiFi password |
| `MQTT_BROKER_HOST` | — | Broker IP or hostname |
| `MQTT_BROKER_PORT` | `1883` | Broker port |
| `MQTT_PUBLISH_INTERVAL_MS` | `30000` | Telemetry interval (ms) |
| `SENSOR_SAMPLE_INTERVAL_MS` | `5000` | DHT sample interval (ms, min 2000) |
| `TEMP_WARN_HIGH_C` | `35.0` | High temp alert threshold |
| `HUMIDITY_WARN_HIGH_PCT` | `90.0` | High humidity alert threshold |
| `RUN_SELF_TESTS` | `1` | Run startup self-tests (0 = skip) |
