#include "fill_light.h"

#include "config.h"
#include "telemetry.h"

static bool          g_on               = false;
static bool          g_lastButtonState  = HIGH;
static unsigned long g_lastDebounceMs   = 0;

static void setFillLight(bool on, const char* reason) {
  if (g_on == on) {
    return;
  }
  g_on = on;
  digitalWrite(FILL_LIGHT_RELAY_PIN, on ? HIGH : LOW);
  Serial.printf("Fill Light: %s (%s)\n", on ? "ON" : "OFF", reason);
  telemetryPublishActuatorStatus();
}

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

bool fillLightHandleMqtt(const String& message) {
  const String cmd = message;
  if (cmd == "1") {
    setFillLight(true, "mqtt");
    return true;
  }
  if (cmd == "0") {
    setFillLight(false, "mqtt");
    return true;
  }
  Serial.println("Fill Light: invalid MQTT payload (use 1 or 0)");
  return false;
}

void fillLightMaintain() {
  const int reading = digitalRead(FILL_LIGHT_BUTTON_PIN);
  const unsigned long now = millis();

  if (reading != g_lastButtonState) {
    g_lastDebounceMs = now;
  }

  if ((now - g_lastDebounceMs) > DEBOUNCE_DELAY_MS) {
    if (reading == LOW && !g_on) {
      setFillLight(true, "button");
      delay(250);
    } else if (reading == LOW && g_on) {
      setFillLight(false, "button");
      delay(250);
    }
  }

  g_lastButtonState = reading;
}
