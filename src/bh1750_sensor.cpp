#include "bh1750_sensor.h"

#include <Wire.h>
#include "config.h"

static bool g_present = false;

static bool bh1750ProbeAddress() {
  Wire.beginTransmission(BH1750_ADDRESS);
  const uint8_t err = Wire.endTransmission();
  Serial.printf("BH1750 I2C probe 0x%02X -> %s\n",
                BH1750_ADDRESS, err == 0 ? "ACK" : "NACK");
  return err == 0;
}

static bool bh1750WriteCommand(uint8_t command) {
  Wire.beginTransmission(BH1750_ADDRESS);
  Wire.write(command);
  return Wire.endTransmission() == 0;
}

static bool bh1750Start() {
  if (!bh1750WriteCommand(0x01)) {
    Serial.println("BH1750: reset failed");
    return false;
  }
  delay(10);

  if (!bh1750WriteCommand(0x10)) {
    Serial.println("BH1750: power-on failed");
    return false;
  }
  delay(10);

  if (!bh1750WriteCommand(0x11)) {
    Serial.println("BH1750: continuous HR mode 2 failed");
    return false;
  }

  delay(180);
  Serial.println("BH1750: continuous HR mode 2 active");
  return true;
}

void bh1750Init() {
  g_present = false;
  if (!bh1750ProbeAddress()) {
    return;
  }
  g_present = bh1750Start();
}

bool bh1750Present() {
  return g_present;
}

uint16_t bh1750ReadLux() {
  const int received = Wire.requestFrom(BH1750_ADDRESS, static_cast<uint8_t>(2));
  if (received < 2 || Wire.available() < 2) {
    Serial.println("BH1750: lux read failed");
    return 0;
  }

  const uint8_t high = static_cast<uint8_t>(Wire.read());
  const uint8_t low  = static_cast<uint8_t>(Wire.read());
  const uint16_t raw = (static_cast<uint16_t>(high) << 8) | low;
  const uint16_t lux = static_cast<uint16_t>(static_cast<float>(raw) / 1.2f);

  Serial.printf("BH1750: raw=%u lux=%u\n", raw, lux);
  return lux;
}
