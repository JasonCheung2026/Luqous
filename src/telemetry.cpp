#include "telemetry.h"

#include <ArduinoJson.h>
#include "bh1750_sensor.h"
#include "config.h"
#include "csv_logger.h"
#include "dht11_sensor.h"
#include "fill_light.h"
#include "mqtt_manager.h"
#include "soil_sensor.h"
#include "water_level.h"
#include "water_valve.h"
#include "wifi_manager.h"

static float    g_temperature   = 0.0f;
static float    g_humidity      = 0.0f;
static uint16_t g_lux           = 0;
static uint16_t g_waterRaw      = 0;
static float    g_soilMoisture  = 0.0f;
static float    g_soilTemp      = 0.0f;
static uint16_t g_soilEc        = 0;
static bool     g_soilValid     = false;
static bool     g_climateValid  = false;

void telemetryFillSnapshot(DisplaySnapshot* out) {
  if (out == nullptr) {
    return;
  }
  out->temperature   = g_temperature;
  out->humidity      = g_humidity;
  out->lux           = g_lux;
  out->waterRaw      = g_waterRaw;
  out->soilMoisture  = g_soilMoisture;
  out->soilTemp      = g_soilTemp;
  out->soilEc        = g_soilEc;
  out->soilValid     = g_soilValid;
  out->climateValid  = g_climateValid;
  out->dht11Present  = dht11Present();
  out->bh1750Present = bh1750Present();
  out->fillLightOn   = fillLightIsOn();
  out->valveOpen     = waterValveIsOpen();
  out->valveAuto     = waterValveAutoEnabled();
  out->wifiOk        = wifiIsConnected();
  out->mqttOk        = mqttIsConnected();
}

static void publishTelemetry(unsigned long timestampMs) {
  DynamicJsonDocument doc(640);
  doc["timestamp"]         = timestampMs;
  doc["datetime"]          = csvLoggerFormatTimestamp();
  doc["temperature"]       = g_temperature;
  doc["humidity"]          = g_humidity;
  doc["lux"]               = g_lux;
  doc["water_raw"]         = g_waterRaw;
  doc["fill_light"]        = csvLoggerFillLightLabel(fillLightIsOn());
  doc["water_valve"]       = csvLoggerOnOffLabel(waterValveIsOpen());
  doc["water_valve_auto"]  = waterValveAutoEnabled();
  doc["soil_moisture_pct"] = g_soilMoisture;
  doc["soil_temp_c"]       = g_soilTemp;
  doc["soil_ec_us_cm"]     = g_soilEc;
  doc["device"]            = "ESP32S3";

  String payload;
  serializeJson(doc, payload);

  Serial.print("Publishing telemetry: ");
  Serial.println(payload);

  if (mqttIsConnected()) {
    const bool ok = mqttPublish(TOPIC_PUBLISH, payload.c_str());
    Serial.printf("MQTT publish %s\n", ok ? "OK" : "FAILED");
  } else {
    Serial.println("MQTT publish skipped (not connected)");
  }
}

void telemetryPrimeCache() {
  if (dht11Present()) {
    float temp = 0.0f;
    float hum  = 0.0f;
    if (dht11Read(&temp, &hum)) {
      g_temperature  = temp;
      g_humidity     = hum;
      g_climateValid = true;
    }
  }
  if (bh1750Present()) {
    g_lux = bh1750ReadLux();
  }
  g_waterRaw = waterLevelReadRaw();
  waterValveMaintainAuto(g_waterRaw);

  if (soilSensorPresent()) {
    SoilSensorReading soil;
    if (soilSensorRead(&soil) && soil.valid) {
      g_soilMoisture = soil.moisturePct;
      g_soilTemp     = soil.temperatureC;
      g_soilEc       = soil.ecUsCm;
      g_soilValid    = true;
    }
  }
}

void telemetryCollectAndPublish() {
  const unsigned long now = millis();

  float temperature = 0.0f;
  float humidity    = 0.0f;

  Serial.println();
  Serial.println("--- Telemetry cycle ---");

  if (dht11Present()) {
    if (dht11Read(&temperature, &humidity)) {
      g_temperature  = temperature;
      g_humidity     = humidity;
      g_climateValid = true;
    }
  } else {
    Serial.println("DHT11: not present, using 0.0 defaults");
  }
  yield();

  if (bh1750Present()) {
    g_lux = bh1750ReadLux();
    delay(180);
  } else {
    Serial.println("BH1750: not present, lux=0");
  }
  yield();

  g_waterRaw = waterLevelReadRaw();
  waterValveMaintainAuto(g_waterRaw);
  yield();

  SoilSensorReading soil;
  if (soilSensorShouldRead() && soilSensorRead(&soil) && soil.valid) {
    g_soilMoisture = soil.moisturePct;
    g_soilTemp     = soil.temperatureC;
    g_soilEc       = soil.ecUsCm;
    g_soilValid    = true;
  } else if (soilSensorPresent()) {
    g_soilValid = false;
    Serial.println("Soil sensor: read failed");
  }
  yield();

  publishTelemetry(now);

  csvLoggerRecordReading(g_temperature, g_humidity, g_lux, g_waterRaw,
                         fillLightIsOn(), waterValveIsOpen(),
                         g_soilMoisture, g_soilTemp, g_soilEc);

  Serial.println("--- End telemetry cycle ---");
}
