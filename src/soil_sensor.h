#pragma once

#include <Arduino.h>

#ifndef SOIL_RS485_DE_RE_PIN
#define SOIL_RS485_DE_RE_PIN 7
#endif
#ifndef SOIL_UART_TX_PIN
#define SOIL_UART_TX_PIN 17
#endif
#ifndef SOIL_UART_RX_PIN
#define SOIL_UART_RX_PIN 18
#endif
#ifndef SOIL_MODBUS_ADDR
#define SOIL_MODBUS_ADDR 1
#endif
#ifndef SOIL_BAUD
#define SOIL_BAUD 4800
#endif

struct SoilSensorReading {
  float    moisturePct;   // volumetric water content, %
  float    temperatureC;  // soil temperature, °C
  uint16_t ecUsCm;        // electrical conductivity, µS/cm
  bool     valid;
};

void soilSensorInit();
bool soilSensorPresent();
bool soilSensorShouldRead();
bool soilSensorRead(SoilSensorReading* out);
