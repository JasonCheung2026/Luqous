#include "water_valve.h"

#include "config.h"
#include "telemetry.h"

static bool          g_open             = false;
static bool          g_lastButtonState  = HIGH;
static unsigned long g_lastDebounceMs   = 0;

static void setValveOpen(bool open, const char* reason) {
  if (g_open == open) {
    return;
  }
  g_open = open;
  digitalWrite(VALVE_RELAY_PIN, open ? HIGH : LOW);
  Serial.printf("Water valve: %s (%s)\n", open ? "OPEN" : "CLOSED", reason);
  telemetryPublishActuatorStatus();
}

void waterValveInit() {
  pinMode(VALVE_BUTTON_PIN, INPUT_PULLUP);
  pinMode(VALVE_RELAY_PIN, OUTPUT);
  digitalWrite(VALVE_RELAY_PIN, LOW);
  g_open = false;
  Serial.printf("Water valve: Button GPIO%u, Relay GPIO%u (boot CLOSED)\n",
                VALVE_BUTTON_PIN, VALVE_RELAY_PIN);
}

bool waterValveIsOpen() {
  return g_open;
}

bool waterValveHandleMqtt(const String& message) {
  const String cmd = message;
  if (cmd == "1") {
    setValveOpen(true, "mqtt");
    return true;
  }
  if (cmd == "0") {
    setValveOpen(false, "mqtt");
    return true;
  }
  Serial.println("Water valve: invalid MQTT payload (use 1 or 0)");
  return false;
}

void waterValveMaintain() {
  const int reading = digitalRead(VALVE_BUTTON_PIN);
  const unsigned long now = millis();

  if (reading != g_lastButtonState) {
    g_lastDebounceMs = now;
  }
  g_lastButtonState = reading;

  if ((now - g_lastDebounceMs) <= DEBOUNCE_DELAY_MS) {
    return;
  }

  static bool buttonStable = HIGH;
  if (reading == buttonStable) {
    return;
  }
  buttonStable = reading;
  if (buttonStable == LOW) {
    setValveOpen(!g_open, "button");
  }
}
