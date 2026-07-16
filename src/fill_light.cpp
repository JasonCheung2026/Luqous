#include "fill_light.h"

#include "config.h"

static bool          g_on               = false;
static bool          g_lastButtonState  = HIGH;
static unsigned long g_lastDebounceMs   = 0;

void fillLightInit() {
  pinMode(FILL_LIGHT_BUTTON_PIN, INPUT_PULLUP);
  pinMode(FILL_LIGHT_RELAY_PIN, OUTPUT);
  digitalWrite(FILL_LIGHT_RELAY_PIN, LOW);
  g_on = false;
  Serial.printf("Fill Light: Button GPIO%u, Relay GPIO%u\n",
                FILL_LIGHT_BUTTON_PIN, FILL_LIGHT_RELAY_PIN);
}

bool fillLightIsOn() {
  return g_on;
}

void fillLightMaintain() {
  const int reading = digitalRead(FILL_LIGHT_BUTTON_PIN);
  const unsigned long now = millis();

  if (reading != g_lastButtonState) {
    g_lastDebounceMs = now;
  }

  if ((now - g_lastDebounceMs) > DEBOUNCE_DELAY_MS) {
    if (reading == LOW && !g_on) {
      g_on = true;
      digitalWrite(FILL_LIGHT_RELAY_PIN, HIGH);
      Serial.println("Fill Light: ON");
      delay(250);
    } else if (reading == LOW && g_on) {
      g_on = false;
      digitalWrite(FILL_LIGHT_RELAY_PIN, LOW);
      Serial.println("Fill Light: OFF");
      delay(250);
    }
  }

  g_lastButtonState = reading;
}
