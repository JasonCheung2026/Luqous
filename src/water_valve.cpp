#include "water_valve.h"

#include <ArduinoJson.h>
#include "config.h"

static bool          g_open             = false;
static bool          g_autoEnabled      = false;
static bool          g_lastButtonState  = HIGH;
static unsigned long g_lastDebounceMs   = 0;
static uint16_t      g_lastWaterRaw     = 0;

static void setValveOpen(bool open, const char* reason) {
  if (g_open == open) {
    return;
  }
  g_open = open;
  digitalWrite(VALVE_RELAY_PIN, open ? HIGH : LOW);
  Serial.printf("Water valve: %s (%s)\n", open ? "OPEN" : "CLOSED", reason);
}

void waterValveInit() {
  pinMode(VALVE_BUTTON_PIN, INPUT_PULLUP);
  pinMode(VALVE_RELAY_PIN, OUTPUT);
  digitalWrite(VALVE_RELAY_PIN, LOW);
  g_open = false;
  g_autoEnabled = false;
  Serial.printf("Water valve: Button GPIO%u, Relay GPIO%u (boot CLOSED, auto OFF)\n",
                VALVE_BUTTON_PIN, VALVE_RELAY_PIN);
  Serial.printf("Water valve: auto thresholds open<%u close>%u (ADC raw)\n",
                VALVE_AUTO_OPEN_BELOW, VALVE_AUTO_CLOSE_ABOVE);
}

bool waterValveIsOpen() {
  return g_open;
}

bool waterValveAutoEnabled() {
  return g_autoEnabled;
}

void waterValveMaintainAuto(uint16_t waterRaw) {
  g_lastWaterRaw = waterRaw;
  if (!g_autoEnabled) {
    return;
  }

  if (waterRaw < VALVE_AUTO_OPEN_BELOW) {
    setValveOpen(true, "auto: water low");
  } else if (waterRaw > VALVE_AUTO_CLOSE_ABOVE) {
    setValveOpen(false, "auto: water high");
  }
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
    g_autoEnabled = false;
    setValveOpen(!g_open, "button");
  }
}

bool waterValveHandleCommand(const String& message) {
  String cmd = message;
  cmd.trim();

  if (cmd.equalsIgnoreCase("valve:on") || cmd.equalsIgnoreCase("valve:open")) {
    g_autoEnabled = false;
    setValveOpen(true, "mqtt");
    return true;
  }
  if (cmd.equalsIgnoreCase("valve:off") || cmd.equalsIgnoreCase("valve:close")) {
    g_autoEnabled = false;
    setValveOpen(false, "mqtt");
    return true;
  }
  if (cmd.equalsIgnoreCase("valve:auto")) {
    g_autoEnabled = true;
    Serial.println("Water valve: auto ENABLED");
    waterValveMaintainAuto(g_lastWaterRaw);
    return true;
  }
  if (cmd.equalsIgnoreCase("valve:manual")) {
    g_autoEnabled = false;
    Serial.println("Water valve: auto DISABLED (manual)");
    return true;
  }

  DynamicJsonDocument doc(256);
  if (deserializeJson(doc, cmd)) {
    return false;
  }

  const char* jsonCmd = doc["cmd"] | "";
  String c(jsonCmd);
  c.toLowerCase();

  if (c == "valve_on" || c == "valve_open") {
    g_autoEnabled = false;
    setValveOpen(true, "mqtt json");
    return true;
  }
  if (c == "valve_off" || c == "valve_close") {
    g_autoEnabled = false;
    setValveOpen(false, "mqtt json");
    return true;
  }
  if (c == "valve_auto") {
    const bool enable = doc["enable"] | true;
    g_autoEnabled = enable;
    Serial.printf("Water valve: auto %s\n", enable ? "ENABLED" : "DISABLED");
    if (enable) {
      waterValveMaintainAuto(g_lastWaterRaw);
    }
    return true;
  }
  if (c == "valve_manual") {
    g_autoEnabled = false;
    Serial.println("Water valve: auto DISABLED (manual)");
    return true;
  }

  return false;
}
