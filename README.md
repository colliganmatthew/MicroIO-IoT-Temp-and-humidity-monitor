# ESP32 Environmental Monitor

ESP32 · FreeRTOS · MQTT · PlatformIO

A modular ESP32 monitoring platform built on a custom FreeRTOS multi-task
architecture. Each sensor, communication channel, and output runs as an
independent pinned task with mutex-protected shared state. 
Designed to be extended by adding a new sensor by creating a task file, adding fields to
shared state, and registering SCPI commands.

Currently monitors temperature and humidity (DHT11) and motion (HC-SR501),
displays readings on an SSD1306 OLED, publishes to MQTT on a configurable
interval, and exposes full runtime control via SCPI commands over serial.

---

## Hardware

| Component     | Connection                                    |
|---------------|-----------------------------------------------|
| DHT11         | DATA → GPIO 4 (change `DHT_PIN` in config.h)  |
| HC-SR501 PIR  | OUT  → GPIO 5 (change `PIR_PIN` in config.h)  |
| SSD1306 OLED  | SDA → GPIO 21, SCL → GPIO 22 (default I2C)   |

---

## Project structure

```
src/
├── main.cpp                   Entry point — init, self-tests, task launch
├── config.h                   All tunables: pins, timings, credentials, thresholds
├── core/
│   ├── debug.h                DBG() macro — compile-time toggle, zero cost when off
│   ├── shared_state.h         SensorState struct, mutex, queues, StateLock, pushError()
│   └── shared_state.cpp
├── scpi/
│   ├── scpi_commands.h        Registry interface
│   └── scpi_commands.cpp      All command handlers and registry table
└── tasks/
    ├── task_wifi.h/.cpp       WiFi connect + reconnect
    ├── task_dht.h/.cpp        DHT11 temperature + humidity sampling
    ├── task_pir.h/.cpp        HC-SR501 motion detection
    ├── task_serial.h/.cpp     UART reader → serial queue
    ├── task_scpi.h/.cpp       SCPI dispatcher
    ├── task_mqtt.h/.cpp       MQTT broker connection + telemetry publish
    ├── task_display.h/.cpp    SSD1306 OLED refresh
    └── task_error.h/.cpp      Error event consumer + heap watchdog
```

---

## Quick start

1. Open `src/config.h` and fill in your WiFi credentials, MQTT broker
   details, and GPIO pins for your hardware.
2. Flash: `pio run --target upload`
3. Open serial monitor at 115200 baud.
4. Type `SYST:HELP?` and press Enter to list all SCPI commands.

---

## Debug logging

Controlled by a single flag in `config.h`:

```cpp
#define DEBUG_ENABLED   1   // 1 = verbose, 0 = silent (production)
```

When `DEBUG_ENABLED = 1`, every function across every task prints tagged
output to serial:

```
[DBG][dht     ] T=23.4 C  H=55.0 %  interval=5000 ms
[DBG][pir     ] Motion detected
[DBG][mqtt    ] Publishing telemetry: {"temp":23.40,"hum":55.00,"ts":12345}
[DBG][scpi    ] dispatch: "MEAS:TEMP?"
[DBG][display ] Rendered T=23.4 H=55.0 wifi=1 mqtt=1
```

When `DEBUG_ENABLED = 0`, all `DBG()` calls compile away entirely

---

## Naming conventions

| Prefix | Meaning | Example |
|--------|---------|---------|
| `g_` | Global — shared across files via extern | `g_state`, `g_stateMutex` |
| `s_` | Static — private to one .cpp file only | `s_dht`, `s_buf`, `s_client` |
| `p_` | Pointer — holds a memory address | `p_display` |
| `m_` | Member — belongs to a class/struct instance | `m_temperature` |
| `k_` | Constant — compile-time value | `kMaxRetries` |

---

## Shared state

All inter-task data lives in `SensorState g_state` defined in
`core/shared_state.h`. Always access it through a `StateLock` scope:

```cpp
// Reading
float t;
{ StateLock lock; t = g_state.temperature; }

// Writing
{ StateLock lock; g_state.temperature = reading; }

// Reading multiple fields as a consistent snapshot
float t, h;
bool ok;
{
    StateLock lock;
    t  = g_state.temperature;
    h  = g_state.humidity;
    ok = g_state.sensorOk;
}
// Do slow work (printf, I2C, formatting) here, outside the lock
```

`StateLock` is RAII, the mutex is released automatically when the scope
exits, even on early returns.

---

## Task architecture

| Task | Core | Priority | Role |
|------|------|----------|------|
| WiFi manager  | 0 | 4 | Connect + reconnect WiFi |
| Serial comms  | 0 | 4 | Read UART → push lines to queue |
| DHT sensor    | 0 | 3 | Sample DHT11, write temp + humidity to shared state |
| PIR sensor    | 0 | 3 | Poll HC-SR501, write motion state to shared state |
| SCPI handler  | 0 | 3 | Dispatch commands from serial queue |
| MQTT          | 0 | 2 | Broker connection + periodic telemetry publish |
| OLED display  | 1 | 2 | 500 ms screen refresh from shared state snapshot |
| Error handler | 1 | 1 | Error event queue consumer + heap watchdog |

Each task is independent. A sensor task hanging or being suspended does
not affect any other task. Each task has its own stack sized for its
specific workload.

---

## SCPI commands

Connect a serial terminal at 115200 baud. Commands are case-insensitive.
Send `SYST:HELP?` to print the full list at runtime.

### Measurements

| Command | Description |
|---------|-------------|
| `MEAS:TEMP?` | Query latest temperature (°C) |
| `MEAS:HUM?` | Query latest humidity (%) |
| `MEAS:ALL?` | Query temperature + humidity as JSON |
| `MEAS:MOT?` | Query motion detected: 1=motion, 0=clear |
| `MEAS:MOT:LAST?` | Query timestamp of last detected motion (ms) |

### Sensor control

| Command | Description |
|---------|-------------|
| `SENS:RATE <ms>` | Set DHT sample interval (min 2000 ms) |
| `SENS:RATE?` | Query DHT sample interval |

### MQTT control

| Command | Description |
|---------|-------------|
| `MQTT:INTV <ms>` | Set publish interval (ms) |
| `MQTT:INTV?` | Query publish interval |
| `MQTT:EN ON\|OFF` | Enable/disable periodic publishing |
| `MQTT:EN?` | Query publish enable state |
| `MQTT:PUB` | Force immediate publish |
| `MQTT:STAT?` | Query broker connection: 1=connected |

### Alerts

| Command | Description |
|---------|-------------|
| `ALRT:EN ON\|OFF` | Enable/disable threshold alerts |
| `ALRT:EN?` | Query alert enable state |
| `ALRT:TEMP:HI <val>` | Set high temperature threshold (°C) |
| `ALRT:TEMP:LO <val>` | Set low temperature threshold (°C) |
| `ALRT:HUM:HI <val>` | Set high humidity threshold (%) |
| `ALRT:HUM:LO <val>` | Set low humidity threshold (%) |

### System

| Command | Description |
|---------|-------------|
| `*IDN?` | Instrument identification |
| `*RST` | Reset all parameters to config.h defaults |
| `SYST:ERR?` | Query and clear last error |
| `SYST:HELP?` | List all registered commands |
| `SYST:UPTIME?` | Query uptime (ms) |
| `SYST:HEAP?` | Query free heap (bytes) |
| `WIFI:STAT?` | Query WiFi connection: 1=connected |
| `TEST:ALL` | Run built-in self-test |

---

## MQTT topics

All topics are prefixed with `MQTT_TOPIC_BASE` (default `sensors/esp32`).

| Topic | Content |
|-------|---------|
| `sensors/esp32/temperature` | Temperature float, e.g. `23.40` |
| `sensors/esp32/humidity` | Humidity float, e.g. `55.00` |
| `sensors/esp32/telemetry` | JSON: `{"temp":23.40,"hum":55.00,"ts":12345}` |
| `sensors/esp32/status` | `online` / `offline` (LWT) |

---

## Adding a new sensor

1. **`config.h`** — add pin and timing constants
2. **`core/shared_state.h`** — add fields to `SensorState`
3. **`tasks/task_xxx.h/.cpp`** — create the sampling task
4. **`scpi/scpi_commands.cpp`** — forward declare, write handlers, add registry rows
5. **`tasks/task_display.cpp`** — snapshot new fields, add display row
6. **`main.cpp`** — call `taskXxxSelfTest()` and `taskXxxStart()`

Nothing outside these six touch points needs to change.

---

## Adding a SCPI command

Open `src/scpi/scpi_commands.cpp`:

**1. Add a forward declaration at the top with the others:**
```cpp
static const char* handleMyCmd (const char*);
```

**2. Write the handler function:**
```cpp
static const char* handleMyCmd(const char* cmd) {
    if (strchr(cmd, '?')) {
        float v; { StateLock lock; v = g_state.myValue; }
        snprintf(s_buf, sizeof(s_buf), "%.2f", v);
        DBG("scpi", "MY:CMD? -> %s", s_buf);
        return s_buf;
    }
    const char* p = cmd + strlen("MY:CMD");
    while (*p == ' ') p++;
    float val = atof(p);
    { StateLock lock; g_state.myValue = val; }
    DBG("scpi", "MY:CMD set to %.2f", val);
    return nullptr;
}
```

**3. Add one row to `g_scpiRegistry[]`:**
```cpp
{ "MY:CMD", handleMyCmd, "Description shown by SYST:HELP?" },
```

No other files change.

---

## Configuration reference (`src/config.h`)

| Define | Default | Description |
|--------|---------|-------------|
| `DEBUG_ENABLED` | `1` | Verbose debug output (0 = off, zero cost) |
| `DHT_PIN` | `4` | GPIO for DHT11 data line |
| `PIR_PIN` | `5` | GPIO for HC-SR501 output |
| `PIR_WARMUP_MS` | `30000` | HC-SR501 stabilisation time on power-on |
| `WIFI_SSID` | — | WiFi network name |
| `WIFI_PASSWORD` | — | WiFi password |
| `MQTT_BROKER_HOST` | — | Broker IP or hostname |
| `MQTT_BROKER_PORT` | `1883` | Broker port |
| `MQTT_CLIENT_ID` | `esp32-monitor` | Unique device ID on the broker |
| `MQTT_PUBLISH_INTERVAL_MS` | `30000` | Telemetry publish interval |
| `SENSOR_SAMPLE_INTERVAL_MS` | `5000` | DHT11 sample interval (min 2000) |
| `TEMP_WARN_HIGH_C` | `35.0` | High temperature alert threshold |
| `TEMP_WARN_LOW_C` | `0.0` | Low temperature alert threshold |
| `HUMIDITY_WARN_HIGH_PCT` | `90.0` | High humidity alert threshold |
| `HUMIDITY_WARN_LOW_PCT` | `10.0` | Low humidity alert threshold |
| `HEAP_WARN_BYTES` | `20480` | Heap warning threshold (20 KB) |
| `HEAP_CRIT_BYTES` | `8192` | Heap critical threshold (8 KB) |
| `ERROR_CHECK_MS` | `1000` | Error task health check interval |
| `DISPLAY_REFRESH_MS` | `500` | OLED refresh interval |
| `RUN_SELF_TESTS` | `1` | Run startup self-tests (0 = skip) |
