#include "oled_display.h"

#include <U8g2lib.h>
#include <Wire.h>
#include "config.h"
#include "soil_sensor.h"

// Most 1.3" DST013 modules use SH1106. For 0.96" SSD1306, swap the two lines below.
static U8G2_SH1106_128X64_NONAME_F_HW_I2C g_u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
// static U8G2_SSD1306_128X64_NONAME_F_HW_I2C g_u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

static bool g_present = false;

static bool oledProbeAddress() {
  Wire.beginTransmission(OLED_ADDRESS);
  const uint8_t err = Wire.endTransmission();
  Serial.printf("OLED I2C probe 0x%02X -> %s\n",
                OLED_ADDRESS, err == 0 ? "ACK" : "NACK");
  return err == 0;
}

void oledInit() {
  g_present = false;
  if (!oledProbeAddress()) {
    return;
  }
  g_u8g2.begin();
  g_present = true;
  Serial.println("OLED: Display driver initialized");
}

bool oledPresent() {
  return g_present;
}

static void drawConnDot(int cx, int cy, bool ok) {
  if (ok) {
    g_u8g2.drawDisc(cx, cy, 2);
  } else {
    g_u8g2.drawCircle(cx, cy, 2);
  }
}

// Inverted header: title left, Wi-Fi / MQTT status dots right
static void drawHeader(bool soilPage, const DisplaySnapshot& snap) {
  g_u8g2.setDrawColor(1);
  g_u8g2.drawBox(0, 0, 128, 12);

  g_u8g2.setDrawColor(0);
  g_u8g2.setFont(u8g2_font_6x10_tf);
  g_u8g2.drawStr(3, 9, soilPage ? "SOIL" : "CLIMATE");

  // Page dots when soil sensor is available
  if (soilSensorPresent()) {
    const int baseX = 52;
    if (soilPage) {
      g_u8g2.drawCircle(baseX, 6, 2);
      g_u8g2.drawDisc(baseX + 8, 6, 2);
    } else {
      g_u8g2.drawDisc(baseX, 6, 2);
      g_u8g2.drawCircle(baseX + 8, 6, 2);
    }
  }

  // Labels + filled/empty dots (Wi-Fi, MQTT)
  g_u8g2.setFont(u8g2_font_5x8_tf);
  g_u8g2.drawStr(78, 9, "W");
  drawConnDot(90, 6, snap.wifiOk);
  g_u8g2.drawStr(100, 9, "M");
  drawConnDot(112, 6, snap.mqttOk);

  g_u8g2.setDrawColor(1);
}

// Label left, value right — industrial HMI row style
static void drawMetricRow(int y, const char* label, const char* value) {
  g_u8g2.setFont(u8g2_font_6x10_tf);
  g_u8g2.drawStr(2, y, label);
  g_u8g2.drawStr(128 - 2 - g_u8g2.getStrWidth(value), y, value);
}

// ON = filled pill (inverted text), OFF = outlined pill
static void drawStatusPill(int x, int y, int w, int h, const char* tag, bool on) {
  g_u8g2.setFont(u8g2_font_5x8_tf);
  char text[12];
  snprintf(text, sizeof(text), "%s %s", tag, on ? "ON" : "OFF");

  const int textW = g_u8g2.getStrWidth(text);
  const int textX = x + (w - textW) / 2;
  const int textY = y + h - 2;

  if (on) {
    g_u8g2.drawRBox(x, y, w, h, 2);
    g_u8g2.setDrawColor(0);
    g_u8g2.drawStr(textX, textY, text);
    g_u8g2.setDrawColor(1);
  } else {
    g_u8g2.drawRFrame(x, y, w, h, 2);
    g_u8g2.drawStr(textX, textY, text);
  }
}

static void drawFooter(const DisplaySnapshot& snap) {
  g_u8g2.drawHLine(0, 51, 128);

  // Fill Light + Valve pills
  drawStatusPill(2, 53, 42, 10, "FL", snap.fillLightOn);
  drawStatusPill(46, 53, 42, 10, "VL", snap.valveOpen);

  g_u8g2.setFont(u8g2_font_5x8_tf);
  char water[14];
  snprintf(water, sizeof(water), "W %u", snap.waterRaw);
  g_u8g2.drawStr(128 - 2 - g_u8g2.getStrWidth(water), 61, water);
}

void oledUpdate(const DisplaySnapshot& snap) {
  if (!g_present) {
    return;
  }

  static bool showSoilPage = false;
  if (soilSensorPresent()) {
    showSoilPage = !showSoilPage;
  } else {
    showSoilPage = false;
  }

  g_u8g2.clearBuffer();
  drawHeader(showSoilPage, snap);

  char value[20];

  if (showSoilPage) {
    if (snap.soilValid) {
      snprintf(value, sizeof(value), "%.1f %%", snap.soilMoisture);
    } else {
      snprintf(value, sizeof(value), "--");
    }
    drawMetricRow(24, "Moisture", value);

    if (snap.soilValid) {
      snprintf(value, sizeof(value), "%.1f C", snap.soilTemp);
    } else {
      snprintf(value, sizeof(value), "--");
    }
    drawMetricRow(36, "Soil Temp", value);

    if (snap.soilValid) {
      snprintf(value, sizeof(value), "%u uS", snap.soilEc);
    } else {
      snprintf(value, sizeof(value), "--");
    }
    drawMetricRow(48, "EC", value);
  } else {
    if (snap.dht11Present && snap.climateValid) {
      snprintf(value, sizeof(value), "%.1f C", snap.temperature);
    } else {
      snprintf(value, sizeof(value), "--");
    }
    drawMetricRow(24, "Temp", value);

    if (snap.dht11Present && snap.climateValid) {
      snprintf(value, sizeof(value), "%.1f %%", snap.humidity);
    } else {
      snprintf(value, sizeof(value), "--");
    }
    drawMetricRow(36, "Humidity", value);

    if (snap.bh1750Present) {
      snprintf(value, sizeof(value), "%u lx", snap.lux);
    } else {
      snprintf(value, sizeof(value), "--");
    }
    drawMetricRow(48, "Light", value);
  }

  drawFooter(snap);
  g_u8g2.sendBuffer();
}
