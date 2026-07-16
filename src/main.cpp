#include <Arduino.h>
#include <Wire.h>

#include "bh1750_sensor.h"
#include "config.h"
#include "csv_logger.h"
#include "dht11_sensor.h"
#include "fill_light.h"
#include "mqtt_manager.h"
#include "oled_display.h"
#include "soil_sensor.h"
#include "telemetry.h"
#include "water_level.h"
#include "water_valve.h"
#include "wifi_manager.h"

static unsigned long g_lastTelemetryMs = 0;
static unsigned long g_lastDisplayMs   = 0;

static void initSerial() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);
  delay(3000);
  Serial.println();
  Serial.println("=== ESP32-S3 Telemetry Node ===");
  Serial.flush();
}

static void refreshDisplay() {
  DisplaySnapshot snap;
  telemetryFillSnapshot(&snap);
  oledUpdate(snap);
}

void setup() {
  initSerial();

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setTimeOut(50);
  Wire.setClock(100000);
  Serial.printf("I2C bus ready: SDA=GPIO%u SCL=GPIO%u @ 100 kHz\n",
                I2C_SDA_PIN, I2C_SCL_PIN);

  bh1750Init();
  dht11Init();
  oledInit();
  fillLightInit();
  waterValveInit();
  waterLevelInit();
  soilSensorInit();
  wifiInit();
  mqttInit();
  csvLoggerInit(mqttClient());

  telemetryPrimeCache();
  refreshDisplay();

  g_lastTelemetryMs = millis();
  g_lastDisplayMs   = millis();
  Serial.println("Setup complete");
  Serial.flush();
}

void loop() {
  wifiMaintain();
  csvLoggerMaintainTime(wifiIsConnected());
  csvLoggerMaintainStorage();
  mqttMaintain();
  mqttLoop();

  fillLightMaintain();
  waterValveMaintain();
  yield();

  const unsigned long now = millis();

  if (now - g_lastTelemetryMs >= TELEMETRY_INTERVAL_MS) {
    g_lastTelemetryMs = now;
    telemetryCollectAndPublish();
  }
  yield();

  if (now - g_lastDisplayMs >= DISPLAY_INTERVAL_MS) {
    g_lastDisplayMs = now;
    refreshDisplay();
  }
  yield();

  delay(1);
}
