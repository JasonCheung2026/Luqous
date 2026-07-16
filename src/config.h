#pragma once

#include <Arduino.h>

// =============================================================================
// Network configuration
// =============================================================================
// 0 = personal WPA2 (phone hotspot / home router)
// 1 = WPA2-Enterprise PEAP (CUHK1x / eduroam)
#ifndef WIFI_USE_ENTERPRISE
#define WIFI_USE_ENTERPRISE 1
#endif

#if WIFI_USE_ENTERPRISE
// Campus Wi-Fi — fill in your CUHK email + OnePass password below.
static const char* WIFI_SSID     = "CUHK1x";  // or "eduroam"
static const char* EAP_IDENTITY  = "YOUR_ID@link.cuhk.edu.hk";
static const char* EAP_USERNAME  = "YOUR_ID@link.cuhk.edu.hk";
static const char* EAP_PASSWORD  = "";  // <<< FILL IN: your OnePass password
#else
static const char* WIFI_SSID     = "YOUR_HOTSPOT_SSID";
static const char* WIFI_PASSWORD = "YOUR_HOTSPOT_PASSWORD";
#endif

static const char* MQTT_BROKER     = "10.13.152.3";
static const uint16_t MQTT_PORT    = 1883;
static const char* TOPIC_PUBLISH   = "esp32/telemetry";
static const char* TOPIC_SUBSCRIBE = "esp32/commands";

// =============================================================================
// Hardware pinout
// =============================================================================
static const uint8_t I2C_SDA_PIN       = 8;
static const uint8_t I2C_SCL_PIN       = 9;
static const uint8_t WATER_ADC_PIN     = 1;
static const uint8_t WATER_POWER_PIN   = 4;
static const uint8_t FILL_LIGHT_BUTTON_PIN = 5;
static const uint8_t FILL_LIGHT_RELAY_PIN  = 6;
static const uint8_t VALVE_BUTTON_PIN  = 15;
static const uint8_t VALVE_RELAY_PIN   = 14;
static const uint8_t DHT11_PIN         = 2;

static const uint8_t BH1750_ADDRESS = 0x23;
static const uint8_t OLED_ADDRESS   = 0x3C;

// Water-valve auto thresholds (12-bit ADC 0–4095)
static const uint16_t VALVE_AUTO_OPEN_BELOW  = 800;
static const uint16_t VALVE_AUTO_CLOSE_ABOVE = 2500;

// =============================================================================
// Timing
// =============================================================================
static const uint32_t TELEMETRY_INTERVAL_MS = 5000;
static const uint32_t DISPLAY_INTERVAL_MS   = 2000;
#if WIFI_USE_ENTERPRISE
static const uint32_t WIFI_RETRY_MS         = 15000;
#else
static const uint32_t WIFI_RETRY_MS         = 5000;
#endif
static const uint32_t MQTT_RETRY_MS         = 5000;
static const uint8_t  WATER_SAMPLE_COUNT    = 8;
static const unsigned long DEBOUNCE_DELAY_MS = 50;
