#include "dht11_sensor.h"

#include <DHT.h>
#include "config.h"

static DHT g_dht11(DHT11_PIN, DHT11);
static bool g_present = false;

void dht11Init() {
  g_dht11.begin();
  Serial.printf("DHT11: driver ready on GPIO%u\n", DHT11_PIN);

  const float humidity = g_dht11.readHumidity();
  const float tempC    = g_dht11.readTemperature();
  if (isnan(humidity) || isnan(tempC)) {
    Serial.println("DHT11: initial read failed");
    g_present = false;
    return;
  }

  Serial.printf("DHT11: T=%.1f C RH=%.1f %%\n", tempC, humidity);
  g_present = true;
}

bool dht11Present() {
  return g_present;
}

bool dht11Read(float* temperature, float* humidity) {
  const float humidityPct = g_dht11.readHumidity();
  const float tempC       = g_dht11.readTemperature();

  if (isnan(humidityPct) || isnan(tempC)) {
    Serial.println("DHT11: read failed");
    return false;
  }

  *temperature = tempC;
  *humidity    = humidityPct;

  Serial.printf("DHT11: T=%.1f C RH=%.1f %%\n", tempC, humidityPct);
  return true;
}
