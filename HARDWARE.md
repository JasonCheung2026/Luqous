# ESP32-S3 Hardware Connection Summary

**Board:** ESP32-S3 DevKitC-1

**Pull-up resistors:** Only the **SD card CS** line uses an external **10 kΩ pull-up to 3.3V**. All other sensors use **no external pull-up** (the Fill Light button uses the ESP32 **internal pull-up** in firmware).

---

## 1. I2C bus (shared)

All I2C devices share one bus.

| Signal | ESP32-S3 GPIO | Devices |
|--------|---------------|---------|
| **SDA** | **GPIO 8** | OLED, BH1750 (light), DHTC12 (temp/humidity) |
| **SCL** | **GPIO 9** | OLED, BH1750, DHTC12 |

| Device | I2C address | Function |
|--------|-------------|----------|
| OLED (SH1106, 128×64) | 0x3C | Status display |
| BH1750 | 0x23 | Ambient light (lux) |
| DHTC12 | 0x44 | Temperature & humidity |

**Wiring:** Device SDA → GPIO 8, SCL → GPIO 9, GND → GND, VCC → 3.3V (per module specs).

**Pull-ups:** No external pull-up resistors on I2C for these sensors.

---

## 2. Water level sensor

| Signal | ESP32-S3 GPIO | Notes |
|--------|---------------|-------|
| **ADC input** | **GPIO 1** | Analog read (12-bit) |
| **Sensor power** | **GPIO 4** | Active HIGH — powers sensor only during read |

**Wiring:** Sensor signal → GPIO 1, sensor VCC enable → GPIO 4, GND → GND.

**Pull-ups:** None.

---

## 3. Fill Light (relay + button)

| Signal | ESP32-S3 GPIO | Notes |
|--------|---------------|-------|
| **Button** | **GPIO 5** | Active LOW; internal pull-up enabled in code |
| **Relay** | **GPIO 6** | Output: HIGH = Fill Light ON, LOW = OFF |

**Wiring:** Button one side → GPIO 5, other side → GND. Relay control → GPIO 6.

**Pull-ups:** No external resistor (internal pull-up on GPIO 5 only).

---

## 4. Micro SD card module (SPI)

Connect by **pin label**, not header position.

| SD module pin | ESP32-S3 GPIO | Notes |
|---------------|---------------|-------|
| **GND** | **GND** | Common ground |
| **VCC** | **3.3V** | Not 5V |
| **MISO** | **GPIO 13** | Master In, Slave Out |
| **MOSI** | **GPIO 11** | Master Out, Slave In |
| **SCK** | **GPIO 12** | SPI clock |
| **CS** | **GPIO 10** | Chip select — **10 kΩ pull-up to 3.3V** |

**Pull-ups:** **CS only** — 10 kΩ from **CS (GPIO 10)** to **3.3V**. MISO, MOSI, and SCK have no external pull-ups.

**Storage:** CSV log file `sensor_log.csv` on the microSD card (FAT32).

---

## 5. Power & ground

| Connection | Notes |
|------------|--------|
| **ESP32-S3** | USB or regulated 5V/3.3V per DevKit |
| **All modules** | Share **GND** with the ESP32 |
| **Logic level** | **3.3V** for I2C sensors, SD module, and relay control |
| **SD module** | Optional: 100 µF + 0.1 µF across VCC–GND for stable writes |

---

## 6. Pin map (quick reference)

```
GPIO 1  — Water level ADC
GPIO 4  — Water sensor power enable
GPIO 5  — Fill Light button (internal pull-up)
GPIO 6  — Fill Light relay
GPIO 8  — I2C SDA (OLED, BH1750, DHTC12)
GPIO 9  — I2C SCL
GPIO 10 — SD CS (+ 10k to 3.3V)
GPIO 11 — SD MOSI
GPIO 12 — SD SCK
GPIO 13 — SD MISO
```

---

## 7. Pull-up summary

| Interface | External pull-up? | Detail |
|-----------|-------------------|--------|
| I2C (OLED, BH1750, DHTC12) | **No** | — |
| Water level | **No** | — |
| Fill Light button | **No** (external) | Internal pull-up on GPIO 5 |
| Fill Light relay | **No** | Output only |
| SD card **CS** | **Yes** | **10 kΩ to 3.3V** |
| SD MISO / MOSI / SCK | **No** | — |
