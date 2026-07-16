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

  g_u8g2.setFont(u8g2_font_5x8_tf);
  g_u8g2.drawStr(0, 8, showSoilPage ? "ESP32-S3 SOIL" : "ESP32-S3 NODE");

  String wifiStr = snap.wifiOk ? "W:OK" : "W:--";
  String mqttStr = snap.mqttOk ? "M:OK" : "M:--";
  String netStr  = wifiStr + " " + mqttStr;

  const int netWidth = g_u8g2.getStrWidth(netStr.c_str());
  g_u8g2.drawStr(128 - netWidth, 8, netStr.c_str());
  g_u8g2.drawHLine(0, 11, 128);

  g_u8g2.setFont(u8g2_font_6x10_tf);
  char buf[32];

  if (showSoilPage) {
    if (snap.soilValid) {
      snprintf(buf, sizeof(buf), "Moist: %.1f %%", snap.soilMoisture);
    } else {
      snprintf(buf, sizeof(buf), "Moist: --");
    }
    g_u8g2.drawStr(0, 24, buf);

    if (snap.soilValid) {
      snprintf(buf, sizeof(buf), "STemp: %.1f C", snap.soilTemp);
    } else {
      snprintf(buf, sizeof(buf), "STemp: --");
    }
    g_u8g2.drawStr(0, 37, buf);

    if (snap.soilValid) {
      snprintf(buf, sizeof(buf), "EC:    %u uS/cm", snap.soilEc);
    } else {
      snprintf(buf, sizeof(buf), "EC:    --");
    }
    g_u8g2.drawStr(0, 50, buf);

    snprintf(buf, sizeof(buf), "Water:%u V:%s", snap.waterRaw, snap.valveOpen ? "ON" : "OFF");
    g_u8g2.drawStr(0, 63, buf);
  } else {
    if (snap.dht11Present && snap.climateValid) {
      snprintf(buf, sizeof(buf), "Temp:  %.2f C", snap.temperature);
    } else {
      snprintf(buf, sizeof(buf), "Temp:  --");
    }
    g_u8g2.drawStr(0, 24, buf);

    if (snap.dht11Present && snap.climateValid) {
      snprintf(buf, sizeof(buf), "Humid: %.2f %%", snap.humidity);
    } else {
      snprintf(buf, sizeof(buf), "Humid: --");
    }
    g_u8g2.drawStr(0, 37, buf);

    if (snap.bh1750Present) {
      snprintf(buf, sizeof(buf), "Light: %u Lux", snap.lux);
    } else {
      snprintf(buf, sizeof(buf), "Light: --");
    }
    g_u8g2.drawStr(0, 50, buf);

    snprintf(buf, sizeof(buf), "Water:%u V:%s%s",
             snap.waterRaw,
             snap.valveOpen ? "ON" : "OFF",
             snap.valveAuto ? "A" : "");
    g_u8g2.drawStr(0, 63, buf);
  }

  g_u8g2.sendBuffer();
}
