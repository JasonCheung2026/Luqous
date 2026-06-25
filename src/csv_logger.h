#pragma once

#include <Arduino.h>
#include <PubSubClient.h>

// Micro SD module wired to ESP32-S3 FSPI: MOSI=11, MISO=13, SCK=12, CS=10
#ifndef USE_SD_CARD
#define USE_SD_CARD 1
#endif

#if USE_SD_CARD
#ifndef SD_CS_PIN
#define SD_CS_PIN   10
#endif
#ifndef SD_MOSI_PIN
#define SD_MOSI_PIN 11
#endif
#ifndef SD_MISO_PIN
#define SD_MISO_PIN 13
#endif
#ifndef SD_SCK_PIN
#define SD_SCK_PIN  12
#endif
#ifndef SD_SPI_HZ
#define SD_SPI_HZ   400000
#endif
#endif

static const char* const CSV_LOG_FILENAME     = "/sensor_log.csv";
static const char* const MQTT_TOPIC_LOG_CSV   = "esp32/log/csv";
static const char* const MQTT_TOPIC_LOG_DOWNLOAD = "esp32/log/download";

void csvLoggerInit(PubSubClient* mqttClient);
void csvLoggerMaintainTime(bool wifiConnected);
void csvLoggerMaintainStorage();

bool csvLoggerTimeReady();
String csvLoggerFormatTimestamp();

bool csvLoggerRecordReading(float temperatureC,
                            float humidityPct,
                            uint16_t lux,
                            uint16_t waterRaw,
                            bool fillLightOn,
                            float soilMoisturePct,
                            float soilTempC,
                            uint16_t soilEcUsCm);
void csvLoggerHandleCommand(const String& command);

String csvLoggerFillLightLabel(bool fillLightOn);
