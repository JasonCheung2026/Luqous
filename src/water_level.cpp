#include "water_level.h"

#include "config.h"

void waterLevelInit() {
  pinMode(WATER_POWER_PIN, OUTPUT);
  digitalWrite(WATER_POWER_PIN, LOW);

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  Serial.printf("Water level: ADC GPIO %u (12-bit, 11 dB), power GPIO %u\n",
                WATER_ADC_PIN, WATER_POWER_PIN);
}

uint16_t waterLevelReadRaw() {
  digitalWrite(WATER_POWER_PIN, HIGH);
  delay(20);

  uint32_t accumulator = 0;
  for (uint8_t i = 0; i < WATER_SAMPLE_COUNT; ++i) {
    accumulator += static_cast<uint16_t>(analogRead(WATER_ADC_PIN));
    yield();
    delay(1);
  }

  digitalWrite(WATER_POWER_PIN, LOW);

  const uint16_t average = static_cast<uint16_t>(accumulator / WATER_SAMPLE_COUNT);
  Serial.printf("Water level: %u samples averaged -> raw=%u (power OFF)\n",
                WATER_SAMPLE_COUNT, average);
  return average;
}
