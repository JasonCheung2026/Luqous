# ESP32-S3 Telemetry Node

Firmware for an ESP32-S3 DevKitC-1 that reads environmental sensors, controls a Fill Light relay, publishes telemetry over MQTT, and logs readings to a microSD card as CSV.

## Features

- **DHT11** — temperature and humidity (GPIO 2, single-wire)
- **BH1750** — ambient light (lux)
- **SN-3000 soil sensor** — moisture, soil temperature, EC over RS485 Modbus
- **Water level** — analog sensor with anti-corrosion power gating
- **Fill Light** — pushbutton toggles a relay (logged as `ON` / `OFF`)
- **Water valve** — relay on GPIO 6 (button / MQTT manual control; boot CLOSED)
- **OLED** — 128×64 SH1106 status display (I2C)
- **SD card** — append-only CSV log (`sensor_log.csv`)
- **MQTT** — JSON telemetry, live CSV rows, and full log download on command
- **NTP** — real timestamps (GMT+8)

Sensor data is collected and logged every **5 seconds**.

## Hardware wiring

### I2C bus (shared)

| Device | SDA | SCL | Address |
|--------|-----|-----|---------|
| OLED, BH1750 | GPIO 8 | GPIO 9 | 0x3C, 0x23 |

### Other GPIO

| Function | GPIO |
|----------|------|
| DHT11 data | 2 |
| Water level ADC | 1 |
| Water sensor power (active HIGH) | 4 |
| Fill Light button (active LOW, pull-up) | 5 |
| Fill Light relay | 14 |
| Water valve button (active LOW, pull-up) | 15 |
| Water valve relay | 6 |
| RS485 DE/RE (MAX485) | 7 |
| UART1 TX → MAX485 DI | 17 |
| UART1 RX ← MAX485 RO | 18 |

See [HARDWARE.md](HARDWARE.md) for full wiring (soil sensor 24V power, A/B lines, common ground).

### Micro SD card (SPI)

Connect by **pin label**, not header position.

| SD module | ESP32-S3 |
|-----------|----------|
| GND | GND |
| VCC | **3.3V** (not 5V) |
| MISO | GPIO 13 |
| MOSI | GPIO 11 |
| SCK | GPIO 12 |
| CS | GPIO 10 |

Format the card as **FAT32**. If writes are unreliable, add a 100 µF + 0.1 µF capacitor across VCC/GND on the SD module.

SD settings are in `src/csv_logger.h` (`USE_SD_CARD`, pin defines, `SD_SPI_HZ`).

## Network configuration

Edit Wi-Fi and MQTT settings in `src/config.h`.

### Campus Wi-Fi (CUHK1x / eduroam) — default

`WIFI_USE_ENTERPRISE` is set to `1`. Fill in:

```cpp
static const char* WIFI_SSID     = "CUHK1x";  // or "eduroam"
static const char* EAP_IDENTITY  = "1155xxxxxx@link.cuhk.edu.hk";
static const char* EAP_USERNAME  = "1155xxxxxx@link.cuhk.edu.hk";
static const char* EAP_PASSWORD  = "your-onepass-password";
```

- Students: `StudentID@link.cuhk.edu.hk`
- Staff: `alias@cuhk.edu.hk`
- Password: CUHK OnePass

### Personal hotspot / home router

Set `#define WIFI_USE_ENTERPRISE 0` and use:

```cpp
static const char* WIFI_SSID     = "...";
static const char* WIFI_PASSWORD = "...";
```

### MQTT — HiveMQ Cloud (default)

`MQTT_USE_TLS` is `1` in `src/config.h`. Fill in:

```cpp
static const char* MQTT_BROKER   = "xxxx.s1.eu.hivemq.cloud";
static const uint16_t MQTT_PORT  = 8883;
static const char* MQTT_USER     = "Luquos";
static const char* MQTT_PASSWORD = "your-hivemq-password";
```

MQTTX: Host = cluster URL, Port = **8883**, SSL/TLS **ON**, username + password.

### MQTT — local Mosquitto

Set `#define MQTT_USE_TLS 0` and use your Mac LAN IP on port **1883**. ESP32 and Mac must be on the same reachable network.

```cpp
static const char* MQTT_BROKER   = "10.13.152.3";
static const uint16_t MQTT_PORT  = 1883;
```

## Node namespaces (`node1` / `node2`)

This project publishes and listens on MQTT topics prefixed with the board namespace:
`esp32/<node>/...`

Before flashing each PCB, set `NODE_ID` in `src/config.h`:
- PCB #1: `#define NODE_ID "node1"`
- PCB #2: `#define NODE_ID "node2"`

## MQTT topics

| Topic | Direction | Description |
|-------|-----------|-------------|
| `esp32/<node>/telemetry` | ESP32 → broker | JSON telemetry every 5 s |
| `esp32/<node>/fill_light/status` | ESP32 → broker | Plain `1` or `0` every 5 s + on change |
| `esp32/<node>/water_valve/status` | ESP32 → broker | Plain `1` or `0` every 5 s + on change |
| `esp32/<node>/log/csv` | ESP32 → broker | One raw CSV row every 5 s |
| `esp32/<node>/fill_light/control` | broker → ESP32 | `1` = ON, `0` = OFF |
| `esp32/<node>/water_valve/control` | broker → ESP32 | `1` = OPEN, `0` = CLOSED |
| `esp32/<node>/commands` | broker → ESP32 | `{"cmd":"download_log"}` |
| `esp32/<node>/log/download` | ESP32 → broker | Full CSV file transfer (JSON chunks) |

### Telemetry JSON example

```json
{
  "timestamp": 88836,
  "datetime": "2026-06-15 15:35:29",
  "temperature": 25.5,
  "humidity": 60.2,
  "lux": 218,
  "water_raw": 0,
  "fill_light": "OFF",
  "water_valve": "OFF",
  "soil_moisture_pct": 32.5,
  "soil_temp_c": 22.1,
  "soil_ec_us_cm": 850,
  "device": "ESP32S3"
}
```

### Actuator status (subscribe to watch)

| Topic | Payload |
|-------|---------|
| `esp32/<node>/fill_light/status` | `1` = ON, `0` = OFF |
| `esp32/<node>/water_valve/status` | `1` = OPEN, `0` = CLOSED |

Published every 5 s and immediately when fill light or valve changes.

### MQTT control (manual only)

| Topic | Payload | Action |
|-------|---------|--------|
| `esp32/<node>/fill_light/control` | `1` | Fill Light ON |
| `esp32/<node>/fill_light/control` | `0` | Fill Light OFF |
| `esp32/<node>/water_valve/control` | `1` | Water valve OPEN |
| `esp32/<node>/water_valve/control` | `0` | Water valve CLOSED |
| `esp32/<node>/commands` | `{"cmd":"download_log"}` | Download full CSV via MQTT |

Physical buttons on the PCB still work (GPIO 5 = Fill Light, GPIO 15 = valve).

### Download full log via MQTT

Publish to `esp32/<node>/commands`:

```json
{"cmd":"download_log"}
```

Responses on `esp32/<node>/log/download`:

- `{"status":"start","data":"/sensor_log.csv"}`
- `{"status":"line","line":1,"data":"datetime,temperature_c,..."}`
- `{"status":"end","lines":N}`

## CSV log format

File on SD card: **`sensor_log.csv`**

```csv
datetime,temperature_c,humidity_pct,lux,water_raw,fill_light,water_valve,soil_moisture_pct,soil_temp_c,soil_ec_us_cm
2026-06-15 15:35:29,0.00,0.00,218,0,OFF,OFF,32.5,22.1,850
```

If you already have an older `sensor_log.csv` on the SD card, delete it (or rename it) so a new file is created with the updated header.

Storage is **SD card only** (no internal flash fallback).

## Build and flash

Requires [PlatformIO](https://platformio.org/).

```bash
pio run                  # build
pio run -t upload        # flash
pio device monitor       # serial monitor (115200 baud)
```

Board: `esp32-s3-devkitc-1` (see `platformio.ini`).

## Download CSV on your Mac (MQTT)

A Python script requests the full log from the ESP32 and saves it locally.

**One-time setup:**

```bash
cd scripts
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
```

**Download and open in Excel:**

```bash
.venv/bin/python mqtt_download_csv.py \
  --password 'YOUR_HIVEMQ_PASSWORD' \
  --output ~/Downloads/sensor_log.csv --open
```

Options:

- `--broker HOST` — default HiveMQ Cloud cluster URL
- `--port 8883` — TLS port (use `--no-tls --port 1883` for local Mosquitto)
- `--username` / `--password` — HiveMQ credentials
- `--node` — topic namespace (`node1` / `node2`)
- `--timeout SEC` — wait time for transfer (default: 30)

**Alternative:** power off the ESP32, remove the microSD card, and open `sensor_log.csv` on your computer.

## Serial log — what “working” looks like

```
MQTT: connected
CSV logger: SD card ready (SD, 481 MB, CS=10 MOSI=11 MISO=13 SCK=12)
MQTT publish OK
CSV logger: MQTT row publish OK
CSV logger: recorded row -> 2026-06-15 15:35:29,0.00,0.00,218,0,OFF
```

## Project layout

```
src/
  main.cpp            # setup() / loop() orchestrator only
  config.h            # pins, timing, Wi-Fi / MQTT credentials
  dht11_sensor.*      # DHT11 temperature & humidity
  bh1750_sensor.*     # BH1750 ambient light
  water_level.*       # water level ADC + power gating
  fill_light.*        # Fill Light button + relay
  water_valve.*       # water valve button + relay + MQTT cmds
  wifi_manager.*      # Wi-Fi (WPA2 / WPA2-Enterprise)
  mqtt_manager.*      # MQTT connect, publish, command dispatch
  oled_display.*      # SH1106 OLED UI
  telemetry.*         # 5 s collect → MQTT JSON + CSV row
  soil_sensor.*       # SN-3000 Modbus RTU over RS485
  csv_logger.*        # NTP, SD CSV logging, MQTT log download
scripts/
  mqtt_download_csv.py
  requirements.txt
platformio.ini
```

## Troubleshooting

| Symptom | Things to check |
|---------|-----------------|
| `DHT11: read failed` | Check GPIO 2 wiring, 3.3V power, data pull-up |
| `Soil sensor: Modbus timeout` | 24V on Brown wire, common GND, DE+RE → GPIO 7, swap A/B |
| `Soil sensor: CRC mismatch` | Loose RS485 wiring, wrong baud (default 4800), wrong address |
| `MQTT publish skipped` | Wi-Fi / hotspot, broker reachable, HiveMQ password filled in |
| `MQTT: MQTT_PASSWORD is empty` | Fill HiveMQ password into `MQTT_PASSWORD` in `config.h` |
| `Wi-Fi: EAP_PASSWORD is empty` | Fill OnePass into `EAP_PASSWORD` in `config.h` |
| Campus Wi-Fi fails | Check email identity, OnePass, SSID `CUHK1x`/`eduroam`; wait 15–30 s for PEAP |
| `datetime: NOT_SYNCED` | Wi-Fi must be up for NTP |
| SD init fails | 3.3V power, MISO/MOSI not swapped, FAT32, firm card seat |
| SD errors but `recorded row` | Often marginal power — add cap on SD module |
| Fill Light | Button GPIO 5 (LOW = pressed), relay GPIO 14 |
| Water valve | Button GPIO 15, relay GPIO 6; MQTT `esp32/<node>/water_valve/control` (`1`/`0`) |
| Fill Light | Button GPIO 5, relay GPIO 14; MQTT `esp32/<node>/fill_light/control` (`1`/`0`) |
