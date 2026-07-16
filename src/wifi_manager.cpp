#include "wifi_manager.h"

#include <WiFi.h>
#include <cstring>
#include "config.h"

static unsigned long g_lastRetryMs = 0;

static bool wifiCredentialsReady() {
#if WIFI_USE_ENTERPRISE
  if (EAP_PASSWORD == nullptr || EAP_PASSWORD[0] == '\0') {
    Serial.println("Wi-Fi: EAP_PASSWORD is empty — fill in your OnePass in config.h");
    return false;
  }
  if (strstr(EAP_IDENTITY, "YOUR_ID") != nullptr) {
    Serial.println("Wi-Fi: replace YOUR_ID in EAP_IDENTITY / EAP_USERNAME with your CUHK email");
    return false;
  }
#endif
  return true;
}

static void wifiBeginConnection() {
  if (!wifiCredentialsReady()) {
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(100);

#if WIFI_USE_ENTERPRISE
  Serial.printf("Wi-Fi: connecting to %s (WPA2-Enterprise PEAP, id=%s)\n",
                WIFI_SSID, EAP_IDENTITY);
  WiFi.begin(WIFI_SSID, WPA2_AUTH_PEAP, EAP_IDENTITY, EAP_USERNAME, EAP_PASSWORD);
#else
  Serial.printf("Wi-Fi: connecting to %s (WPA2-Personal)\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
#endif
}

void wifiInit() {
  g_lastRetryMs = millis();
  wifiBeginConnection();
}

void wifiMaintain() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  const unsigned long now = millis();
  if (now - g_lastRetryMs < WIFI_RETRY_MS) {
    return;
  }
  g_lastRetryMs = now;

  Serial.println("Wi-Fi: disconnected, retrying...");
  wifiBeginConnection();
}

bool wifiIsConnected() {
  return WiFi.status() == WL_CONNECTED;
}
