# ESP32-S3 Telemetry Node

Firmware for an ESP32-S3 DevKitC-1 that reads environmental sensors, controls a Fill Light relay, publishes telemetry over MQTT, and logs readings to a microSD card as CSV.

## Features

- **DHTC12** — temperature and humidity (I2C)
- **BH1750** — ambient light (lux)
- **Water level** — analog sensor with anti-corrosion power gating
- **Fill Light** — pushbutton toggles a relay (logged as `ON` / `OFF`)
- **OLED** — 128×64 SH1106 status display (I2C)
- **SD card** — append-only CSV log (`sensor_log.csv`)
- **MQTT** — JSON telemetry, live CSV rows, and full log download on command
- **NTP** — real timestamps (GMT+8)

Sensor data is collected and logged every **5 seconds**.

## Hardware wiring

### I2C bus (shared)

| Device | SDA | SCL | Address |
|--------|-----|-----|---------|
| OLED, BH1750, DHTC12 | GPIO 8 | GPIO 9 | 0x3C, 0x23, 0x44 |

### Other GPIO

| Function | GPIO |
|----------|------|
| Water level ADC | 1 |
| Water sensor power (active HIGH) | 4 |
| Fill Light button (active LOW, pull-up) | 5 |
| Fill Light relay | 6 |

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

Edit Wi-Fi and MQTT settings in `src/main.cpp`:

```cpp
static const char* WIFI_SSID     = "...";
static const char* WIFI_PASSWORD = "...";
static const char* MQTT_BROKER   = "172.20.10.3";  // broker IP on your LAN/hotspot
static const uint16_t MQTT_PORT  = 1883;
```

The ESP32 must reach an MQTT broker (e.g. Mosquitto on your Mac while the hotspot is on).

## MQTT topics

| Topic | Direction | Description |
|-------|-----------|-------------|
| `esp32/telemetry` | ESP32 → broker | JSON telemetry every 5 s |
| `esp32/log/csv` | ESP32 → broker | One raw CSV row every 5 s |
| `esp32/commands` | broker → ESP32 | Commands (subscribe) |
| `esp32/log/download` | ESP32 → broker | Full CSV file transfer (JSON chunks) |

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
  "device": "ESP32S3"
}
```

### Download full log via MQTT

Publish to `esp32/commands`:

```json
{"cmd":"download_log"}
```

Responses on `esp32/log/download`:

- `{"status":"start","data":"/sensor_log.csv"}`
- `{"status":"line","line":1,"data":"datetime,temperature_c,..."}`
- `{"status":"end","lines":N}`

## CSV log format

File on SD card: **`sensor_log.csv`**

```csv
datetime,temperature_c,humidity_pct,lux,water_raw,fill_light
2026-06-15 15:35:29,0.00,0.00,218,0,OFF
```

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
.venv/bin/python mqtt_download_csv.py --output ~/Downloads/sensor_log.csv --open
```

Options:

- `--broker IP` — MQTT broker address (default: `172.20.10.3`)
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
  main.cpp          # sensors, Wi-Fi, MQTT, OLED, relay
  csv_logger.cpp    # NTP, SD CSV logging, MQTT log download
  csv_logger.h      # SD pin config
scripts/
  mqtt_download_csv.py   # download CSV from MQTT to Mac
  requirements.txt
platformio.ini
```

## Troubleshooting

| Symptom | Things to check |
|---------|-----------------|
| `DHTC12: not present` | I2C wiring (GPIO 8/9), sensor power, address 0x44 |
| `MQTT publish skipped` | Wi-Fi / hotspot, broker IP, broker running |
| `datetime: NOT_SYNCED` | Wi-Fi must be up for NTP |
| SD init fails | 3.3V power, MISO/MOSI not swapped, FAT32, firm card seat |
| SD errors but `recorded row` | Often marginal power — add cap on SD module |
| Fill Light | Button GPIO 5 (LOW = pressed), relay GPIO 6 |
