#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <U8g2lib.h>

// =============================================================================
// Network configuration
// =============================================================================
static const char* WIFI_SSID       = "JasonCheung";
static const char* WIFI_PASSWORD   = "51160779";
static const char* MQTT_BROKER     = "172.20.10.3";
static const uint16_t MQTT_PORT    = 1883;
static const char* TOPIC_PUBLISH   = "esp32/telemetry";
static const char* TOPIC_SUBSCRIBE = "esp32/commands";

// =============================================================================
// Hardware pinout
// =============================================================================
static const uint8_t I2C_SDA_PIN      = 8;
static const uint8_t I2C_SCL_PIN      = 9;
static const uint8_t WATER_ADC_PIN    = 1;   // ADC1 channel 0
static const uint8_t WATER_POWER_PIN  = 4;   // sensor VCC enable (active HIGH)

static const uint8_t BUTTON_PIN      = 5;   // Pushbutton pin (active LOW with internal pull-up)
static const uint8_t RELAY_PIN       = 6;   // Relay control output pin (constant 3.3V / 0V)

static const uint8_t BH1750_ADDRESS   = 0x23;
static const uint8_t DHTC12_ADDRESS   = 0x44;
static const uint8_t OLED_ADDRESS     = 0x3C; // Standard 7-bit I2C address for SSD1306/SH1106

// =============================================================================
// Timing
// =============================================================================
static const uint32_t TELEMETRY_INTERVAL_MS = 5000;
static const uint32_t DISPLAY_INTERVAL_MS   = 2000; // Update display every 2 seconds
static const uint32_t WIFI_RETRY_MS         = 5000;
static const uint32_t MQTT_RETRY_MS         = 5000;
static const uint8_t  WATER_SAMPLE_COUNT    = 8;
static const unsigned long DEBOUNCE_DELAY_MS = 50;   // Debounce time for physical button

// =============================================================================
// Global state
// =============================================================================
WiFiClient   wifiClient;
PubSubClient mqttClient(wifiClient);

// Sensor presence flags
bool     bh1750_present  = false;
bool     dhtc12_present  = false;
bool     oled_present    = false;

uint16_t dhtc12_hum_a    = 0;
uint16_t dhtc12_hum_b    = 0;

// Relay and Button states
bool          relay_state        = false;  // False = 0V, True = 3.3V
bool          last_button_state  = HIGH;   // Starts HIGH due to internal pull-up
unsigned long last_debounce_time = 0;

// Cached sensor readings for non-blocking asynchronous display updates
float    latest_temperature = 0.0f;
float    latest_humidity    = 0.0f;
uint16_t latest_lux         = 0;
uint16_t latest_water_raw   = 0;
bool     latest_data_valid  = false;

unsigned long last_telemetry_ms      = 0;
unsigned long last_display_update_ms = 0;
unsigned long last_wifi_retry_ms     = 0;
unsigned long last_mqtt_retry_ms     = 0;

// =============================================================================
// Display Driver Initialization
// =============================================================================
// Most 1.3" DST013 modules use the SH1106 controller chip.
// If your screen is a 0.96" SSD1306 model, comment out the first line and
// uncomment the second line.
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
// U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

// =============================================================================
// Serial helpers
// =============================================================================
static void initSerial() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);
  delay(3000);
  Serial.println();
  Serial.println("=== ESP32-S3 Telemetry Node ===");
  Serial.flush();
}

// =============================================================================
// DHTC12 — CRC-8 (polynomial 0x131, init 0xFF)
// Returns 0 when CRC matches, 1 on mismatch.
// =============================================================================
static uint8_t dhtc12CheckCrc(const uint8_t data[], uint8_t dataLen) {
  uint8_t crc = 0xFF;
  const uint16_t polynomial = 0x131;

  for (uint8_t i = 0; i < dataLen; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 8; bit > 0; --bit) {
      if (crc & 0x80) {
        crc = static_cast<uint8_t>((crc << 1) ^ polynomial);
      } else {
        crc = static_cast<uint8_t>(crc << 1);
      }
    }
  }
  return (crc == data[dataLen]) ? 0 : 1;
}

static bool dhtc12ProbeAddress() {
  Wire.beginTransmission(DHTC12_ADDRESS);
  uint8_t err = Wire.endTransmission();
  Serial.printf("DHTC12 I2C probe 0x%02X -> %s\n",
                DHTC12_ADDRESS, err == 0 ? "ACK" : "NACK");
  return err == 0;
}

static uint8_t dhtc12ReadRegister(uint8_t regAddr) {
  Wire.beginTransmission(DHTC12_ADDRESS);
  Wire.write(0xD2);
  Wire.write(regAddr);
  if (Wire.endTransmission() != 0) {
    Serial.printf("DHTC12: register %u write failed\n", regAddr);
    return 0xFF;
  }

  delayMicroseconds(10);
  yield();

  int received = Wire.requestFrom(DHTC12_ADDRESS, static_cast<uint8_t>(3));
  if (received < 3 || Wire.available() < 3) {
    Serial.printf("DHTC12: register %u read timeout\n", regAddr);
    return 0xFF;
  }

  uint8_t buf[3] = {
    static_cast<uint8_t>(Wire.read()),
    static_cast<uint8_t>(Wire.read()),
    static_cast<uint8_t>(Wire.read())
  };

  if (dhtc12CheckCrc(buf, 2) != 0) {
    Serial.printf("DHTC12: register %u CRC FAIL (raw %02X %02X %02X)\n",
                  regAddr, buf[0], buf[1], buf[2]);
    return 0xFF;
  }

  Serial.printf("DHTC12: register %u OK -> 0x%02X (CRC pass)\n", regAddr, buf[0]);
  return buf[0];
}

static bool dhtc12InitCalibration() {
  Wire.beginTransmission(DHTC12_ADDRESS);
  Wire.write(0x30);
  Wire.write(0xA2);
  if (Wire.endTransmission() != 0) {
    Serial.println("DHTC12: soft reset failed");
    return false;
  }
  delay(10);

  const uint8_t humA_hi = dhtc12ReadRegister(8);
  const uint8_t humA_lo = dhtc12ReadRegister(9);
  const uint8_t humB_hi = dhtc12ReadRegister(10);
  const uint8_t humB_lo = dhtc12ReadRegister(11);

  dhtc12_hum_a = (static_cast<uint16_t>(humA_hi) << 8) | humA_lo;
  dhtc12_hum_b = (static_cast<uint16_t>(humB_hi) << 8) | humB_lo;

  if (dhtc12_hum_a == 0 || dhtc12_hum_b == 0 ||
      dhtc12_hum_a == 0xFFFF || dhtc12_hum_b == 0xFFFF ||
      humA_hi == 0xFF || humA_lo == 0xFF || humB_hi == 0xFF || humB_lo == 0xFF) {
    Serial.println("DHTC12: invalid calibration constants");
    return false;
  }

  Serial.printf("DHTC12 calibration loaded -> HumA=%u HumB=%u\n",
                dhtc12_hum_a, dhtc12_hum_b);
  return true;
}

static bool dhtc12Read(float* temperature, float* humidity) {
  Wire.beginTransmission(DHTC12_ADDRESS);
  Wire.write(0x2C);
  Wire.write(0x10);
  if (Wire.endTransmission() != 0) {
    Serial.println("DHTC12: measurement trigger failed");
    return false;
  }

  bool conversionDone = false;
  const unsigned long start = millis();
  while (millis() - start < 50) {
    Wire.beginTransmission(DHTC12_ADDRESS);
    if (Wire.endTransmission() == 0) {
      conversionDone = true;
      break;
    }
    yield();
    delay(1);
  }

  if (!conversionDone) {
    Serial.println("DHTC12: conversion timeout");
    return false;
  }

  const int received = Wire.requestFrom(DHTC12_ADDRESS, static_cast<uint8_t>(6));
  if (received < 6 || Wire.available() < 6) {
    Serial.println("DHTC12: data read timeout");
    return false;
  }

  uint8_t raw[6];
  for (uint8_t i = 0; i < 6; ++i) {
    raw[i] = static_cast<uint8_t>(Wire.read());
    yield();
  }

  uint8_t tempFrame[3] = {raw[0], raw[1], raw[2]};
  if (dhtc12CheckCrc(tempFrame, 2) != 0) {
    Serial.printf("DHTC12: temperature CRC FAIL (%02X %02X %02X)\n",
                  raw[0], raw[1], raw[2]);
    return false;
  }

  uint8_t humFrame[3] = {raw[3], raw[4], raw[5]};
  if (dhtc12CheckCrc(humFrame, 2) != 0) {
    Serial.printf("DHTC12: humidity CRC FAIL (%02X %02X %02X)\n",
                  raw[3], raw[4], raw[5]);
    return false;
  }

  const uint16_t rawTemp = (static_cast<uint16_t>(raw[0]) << 8) | raw[1];
  const uint16_t rawHum  = (static_cast<uint16_t>(raw[3]) << 8) | raw[4];

  float tempC = -40.0f + (static_cast<float>(rawTemp) / 256.0f);
  float rh    = 30.0f + ((static_cast<float>(rawHum) - dhtc12_hum_b) * 60.0f)
                        / (static_cast<float>(dhtc12_hum_a) - dhtc12_hum_b);
  rh += 0.25f * (tempC - 25.0f);

  if (rh > 100.0f) rh = 100.0f;
  if (rh < 0.0f)   rh = 0.0f;

  *temperature = tempC;
  *humidity    = rh;

  Serial.printf("DHTC12: CRC OK | rawT=%u rawH=%u | T=%.2f C RH=%.2f %%\n",
                rawTemp, rawHum, tempC, rh);
  return true;
}

// =============================================================================
// BH1750 — continuous high-resolution mode 2 (0x11)
// =============================================================================
static bool bh1750ProbeAddress() {
  Wire.beginTransmission(BH1750_ADDRESS);
  uint8_t err = Wire.endTransmission();
  Serial.printf("BH1750 I2C probe 0x%02X -> %s\n",
                BH1750_ADDRESS, err == 0 ? "ACK" : "NACK");
  return err == 0;
}

static bool bh1750WriteCommand(uint8_t command) {
  Wire.beginTransmission(BH1750_ADDRESS);
  Wire.write(command);
  return Wire.endTransmission() == 0;
}

static bool bh1750Init() {
  if (!bh1750WriteCommand(0x01)) {
    Serial.println("BH1750: reset failed");
    return false;
  }
  delay(10);

  if (!bh1750WriteCommand(0x10)) {
    Serial.println("BH1750: power-on failed");
    return false;
  }
  delay(10);

  if (!bh1750WriteCommand(0x11)) {
    Serial.println("BH1750: continuous HR mode 2 failed");
    return false;
  }

  delay(180);
  Serial.println("BH1750: continuous HR mode 2 active");
  return true;
}

static uint16_t bh1750ReadLux() {
  const int received = Wire.requestFrom(BH1750_ADDRESS, static_cast<uint8_t>(2));
  if (received < 2 || Wire.available() < 2) {
    Serial.println("BH1750: lux read failed");
    return 0;
  }

  const uint8_t high = static_cast<uint8_t>(Wire.read());
  const uint8_t low  = static_cast<uint8_t>(Wire.read());
  const uint16_t raw = (static_cast<uint16_t>(high) << 8) | low;
  const uint16_t lux = static_cast<uint16_t>(static_cast<float>(raw) / 1.2f);

  Serial.printf("BH1750: raw=%u lux=%u\n", raw, lux);
  return lux;
}

// =============================================================================
// Water level — anti-corrosion power gating
// =============================================================================
static void waterLevelInit() {
  pinMode(WATER_POWER_PIN, OUTPUT);
  digitalWrite(WATER_POWER_PIN, LOW);

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  Serial.printf("Water level: ADC GPIO %u (12-bit, 11 dB), power GPIO %u\n",
                WATER_ADC_PIN, WATER_POWER_PIN);
}

static uint16_t waterLevelReadRaw() {
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

// =============================================================================
// Wi-Fi — non-blocking reconnect
// =============================================================================
static void wifiInit() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  last_wifi_retry_ms = millis();
  Serial.print("Wi-Fi: connecting to ");
  Serial.println(WIFI_SSID);
}

static void wifiMaintain() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  const unsigned long now = millis();
  if (now - last_wifi_retry_ms < WIFI_RETRY_MS) {
    return;
  }
  last_wifi_retry_ms = now;

  Serial.println("Wi-Fi: disconnected, retrying...");
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

static bool wifiIsConnected() {
  return WiFi.status() == WL_CONNECTED;
}

// =============================================================================
// MQTT — non-blocking reconnect
// =============================================================================
static void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.println();
  Serial.println("--- MQTT command received ---");
  Serial.printf("Topic: %s\n", topic);

  String message;
  message.reserve(length);
  for (unsigned int i = 0; i < length; ++i) {
    message += static_cast<char>(payload[i]);
    yield();
  }

  Serial.print("Payload: ");
  Serial.println(message);
  Serial.println("-----------------------------");
}

static void mqttInit() {
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(512);
}

static void mqttMaintain() {
  if (!wifiIsConnected()) {
    return;
  }

  if (mqttClient.connected()) {
    return;
  }

  const unsigned long now = millis();
  if (now - last_mqtt_retry_ms < MQTT_RETRY_MS) {
    return;
  }
  last_mqtt_retry_ms = now;

  const String clientId = "ESP32S3Client-" + String(random(0, 1000));
  Serial.print("MQTT: connecting... ");

  if (mqttClient.connect(clientId.c_str())) {
    Serial.println("connected");
    mqttClient.subscribe(TOPIC_SUBSCRIBE);
    Serial.printf("MQTT: subscribed to %s\n", TOPIC_SUBSCRIBE);
  } else {
    Serial.printf("failed (rc=%d), retry in %lu ms\n",
                  mqttClient.state(), MQTT_RETRY_MS);
  }
}

// =============================================================================
// Button & Relay Driver
// =============================================================================
static void relayButtonInit() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW); // Start with 0V on the relay pin
  Serial.printf("Relay & Button: Button Pin GPIO%u, Relay Pin GPIO%u\n", BUTTON_PIN, RELAY_PIN);
}

static void maintainRelayButton() {
  int reading = digitalRead(BUTTON_PIN);
  const unsigned long now = millis();

  // If the physical state of the switch changed
  if (reading != last_button_state) {
    last_debounce_time = now;
  }

  if ((now - last_debounce_time) > DEBOUNCE_DELAY_MS) {
    // If the button is pressed (LOW due to INPUT_PULLUP)
    if (reading == LOW && !relay_state) {
      relay_state = true;
      digitalWrite(RELAY_PIN, HIGH); // Output constant 3.3V
      Serial.println("Relay State: ON (3.3V output)");
      delay(250); // Guard to prevent rapid re-triggering from a single press
    } 
    else if (reading == LOW && relay_state) {
      relay_state = false;
      digitalWrite(RELAY_PIN, LOW);  // Output 0V
      Serial.println("Relay State: OFF (0V output)");
      delay(250); // Guard to prevent rapid re-triggering from a single press
    }
  }

  last_button_state = reading;
}

// =============================================================================
// OLED — Display management helper
// =============================================================================
static bool oledProbeAddress() {
  Wire.beginTransmission(OLED_ADDRESS);
  uint8_t err = Wire.endTransmission();
  Serial.printf("OLED I2C probe 0x%02X -> %s\n",
                OLED_ADDRESS, err == 0 ? "ACK" : "NACK");
  return err == 0;
}

static void updateDisplay() {
  if (!oled_present) {
    return;
  }

  u8g2.clearBuffer();

  // 1. Draw top status bar
  u8g2.setFont(u8g2_font_5x8_tf);
  u8g2.drawStr(0, 8, "ESP32-S3 NODE");

  // Format connection status indicators
  String wifiStr = wifiIsConnected() ? "W:OK" : "W:--";
  String mqttStr = mqttClient.connected() ? "M:OK" : "M:--";
  String netStr  = wifiStr + " " + mqttStr;
  
  int netWidth = u8g2.getStrWidth(netStr.c_str());
  u8g2.drawStr(128 - netWidth, 8, netStr.c_str());

  // Horizontal separating line
  u8g2.drawHLine(0, 11, 128);

  // 2. Main layout lines (Y offsets spaced to fit cleanly)
  u8g2.setFont(u8g2_font_6x10_tf);
  char buf[32];

  // Row 1: Temperature
  if (dhtc12_present && latest_data_valid) {
    snprintf(buf, sizeof(buf), "Temp:  %.2f C", latest_temperature);
  } else {
    snprintf(buf, sizeof(buf), "Temp:  --");
  }
  u8g2.drawStr(0, 24, buf);

  // Row 2: Humidity
  if (dhtc12_present && latest_data_valid) {
    snprintf(buf, sizeof(buf), "Humid: %.2f %%", latest_humidity);
  } else {
    snprintf(buf, sizeof(buf), "Humid: --");
  }
  u8g2.drawStr(0, 37, buf);

  // Row 3: Light
  if (bh1750_present) {
    snprintf(buf, sizeof(buf), "Light: %u Lux", latest_lux);
  } else {
    snprintf(buf, sizeof(buf), "Light: --");
  }
  u8g2.drawStr(0, 50, buf);

  // Row 4: Water Level (Raw)
  snprintf(buf, sizeof(buf), "Water: %u", latest_water_raw);
  u8g2.drawStr(0, 63, buf);

  u8g2.sendBuffer();
}

// =============================================================================
// Telemetry publish
// =============================================================================
static void publishTelemetry(unsigned long timestampMs,
                             float temperature,
                             float humidity,
                             uint16_t lux,
                             uint16_t waterRaw) {
  DynamicJsonDocument doc(256);
  doc["timestamp"]   = timestampMs;
  doc["temperature"] = temperature;
  doc["humidity"]    = humidity;
  doc["lux"]         = lux;
  doc["water_raw"]   = waterRaw;
  doc["device"]      = "ESP32S3";

  String payload;
  serializeJson(doc, payload);

  Serial.print("Publishing telemetry: ");
  Serial.println(payload);

  if (mqttClient.connected()) {
    const bool ok = mqttClient.publish(TOPIC_PUBLISH, payload.c_str());
    Serial.printf("MQTT publish %s\n", ok ? "OK" : "FAILED");
  } else {
    Serial.println("MQTT publish skipped (not connected)");
  }
}

static void collectAndPublishTelemetry() {
  const unsigned long now = millis();

  float temperature = 0.0f;
  float humidity    = 0.0f;
  uint16_t lux      = 0;
  uint16_t waterRaw = 0;

  Serial.println();
  Serial.println("--- Telemetry cycle ---");

  if (dhtc12_present) {
    if (dhtc12Read(&temperature, &humidity)) {
      latest_temperature = temperature;
      latest_humidity    = humidity;
      latest_data_valid  = true;
    } else {
      Serial.println("DHTC12: read failed");
    }
  } else {
    Serial.println("DHTC12: not present, using 0.0 defaults");
  }
  yield();

  if (bh1750_present) {
    lux = bh1750ReadLux();
    latest_lux = lux;
    delay(180);
  } else {
    Serial.println("BH1750: not present, lux=0");
  }
  yield();

  waterRaw = waterLevelReadRaw();
  latest_water_raw = waterRaw;
  yield();

  publishTelemetry(now, latest_temperature, latest_humidity, latest_lux, latest_water_raw);
  Serial.println("--- End telemetry cycle ---");
}

// =============================================================================
// Arduino entry points
// =============================================================================
void setup() {
  initSerial();

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setTimeOut(50);
  Wire.setClock(100000);
  Serial.printf("I2C bus ready: SDA=GPIO%u SCL=GPIO%u @ 100 kHz\n",
                I2C_SDA_PIN, I2C_SCL_PIN);

  // Probing and initializing devices sequentially
  if (bh1750ProbeAddress()) {
    bh1750_present = bh1750Init();
  }

  if (dhtc12ProbeAddress()) {
    dhtc12_present = dhtc12InitCalibration();
  }

  if (oledProbeAddress()) {
    // If your display board address is configured differently, uncomment below:
    // u8g2.setI2CAddress(OLED_ADDRESS * 2); 
    u8g2.begin();
    oled_present = true;
    Serial.println("OLED: Display driver initialized");
  }

  relayButtonInit(); // Initialize the Relay and Button GPIO setup
  waterLevelInit();
  wifiInit();
  mqttInit();

  // Perform a rapid initial sensor reading to populate cached states 
  // so the OLED does not display empty "--" values on bootup.
  if (dhtc12_present) {
    float temp = 0.0f, hum = 0.0f;
    if (dhtc12Read(&temp, &hum)) {
      latest_temperature = temp;
      latest_humidity    = hum;
      latest_data_valid  = true;
    }
  }
  if (bh1750_present) {
    latest_lux = bh1750ReadLux();
  }
  latest_water_raw = waterLevelReadRaw();

  // Initial render immediately after cache is populated
  updateDisplay();

  last_telemetry_ms = millis();
  last_display_update_ms = millis();
  Serial.println("Setup complete");
  Serial.flush();
}

void loop() {
  wifiMaintain();
  mqttMaintain();
  mqttClient.loop();

  maintainRelayButton(); // Continuously monitor and switch the relay based on the button
  yield();

  const unsigned long now = millis();

  // Telemetry cycle (Non-blocking, every 5s)
  if (now - last_telemetry_ms >= TELEMETRY_INTERVAL_MS) {
    last_telemetry_ms = now;
    collectAndPublishTelemetry();
  }
  yield();

  // Display cycle (Non-blocking, every 2s)
  if (now - last_display_update_ms >= DISPLAY_INTERVAL_MS) {
    last_display_update_ms = now;
    updateDisplay();
  }
  yield();

  delay(1);
}