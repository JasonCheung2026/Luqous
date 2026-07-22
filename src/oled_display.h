#pragma once

#include <Arduino.h>

struct DisplaySnapshot {
  float    temperature;
  float    humidity;
  uint16_t lux;
  uint16_t waterRaw;
  float    soilMoisture;
  float    soilTemp;
  uint16_t soilEc;
  bool     soilValid;
  bool     climateValid;
  bool     dht11Present;
  bool     bh1750Present;
  bool     fillLightOn;
  bool     valveOpen;
  bool     wifiOk;
  bool     mqttOk;
};

void oledInit();
bool oledPresent();
void oledUpdate(const DisplaySnapshot& snap);
