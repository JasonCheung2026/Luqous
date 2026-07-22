#include "mqtt_manager.h"

#include <WiFi.h>
#include "config.h"
#if MQTT_USE_TLS
#include <WiFiClientSecure.h>
#endif
#include "csv_logger.h"
#include "fill_light.h"
#include "water_valve.h"
#include "wifi_manager.h"

#if MQTT_USE_TLS
static WiFiClientSecure g_wifiClient;
#else
static WiFiClient g_wifiClient;
#endif
static PubSubClient g_mqttClient(g_wifiClient);
static unsigned long g_lastRetryMs = 0;

static void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.println();
  Serial.println("--- MQTT message received ---");
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

  const String topicStr(topic);
  if (topicStr == TOPIC_FILL_LIGHT) {
    fillLightHandleMqtt(message);
    return;
  }
  if (topicStr == TOPIC_VALVE) {
    waterValveHandleMqtt(message);
    return;
  }
  if (topicStr == TOPIC_COMMANDS) {
    csvLoggerHandleCommand(message);
    return;
  }
}

static bool mqttCredentialsReady() {
#if MQTT_USE_TLS
  if (MQTT_PASSWORD == nullptr || MQTT_PASSWORD[0] == '\0') {
    Serial.println("MQTT: MQTT_PASSWORD is empty — fill in your HiveMQ password in config.h");
    return false;
  }
  if (MQTT_USER == nullptr || MQTT_USER[0] == '\0') {
    Serial.println("MQTT: MQTT_USER is empty — set username in config.h");
    return false;
  }
#endif
  return true;
}

void mqttInit() {
#if MQTT_USE_TLS
  g_wifiClient.setInsecure();
  Serial.printf("MQTT: TLS enabled, broker=%s:%u user=%s\n",
                MQTT_BROKER, MQTT_PORT, MQTT_USER);
#else
  Serial.printf("MQTT: plain TCP, broker=%s:%u\n", MQTT_BROKER, MQTT_PORT);
#endif
  g_mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  g_mqttClient.setCallback(mqttCallback);
  g_mqttClient.setBufferSize(1024);
}

void mqttMaintain() {
  if (!wifiIsConnected()) {
    return;
  }

  if (g_mqttClient.connected()) {
    return;
  }

  if (!mqttCredentialsReady()) {
    return;
  }

  const unsigned long now = millis();
  if (now - g_lastRetryMs < MQTT_RETRY_MS) {
    return;
  }
  g_lastRetryMs = now;

  const String clientId = "ESP32S3Client-" + String(random(0, 1000));
  Serial.print("MQTT: connecting... ");

  bool ok = false;
#if MQTT_USE_TLS
  ok = g_mqttClient.connect(clientId.c_str(), MQTT_USER, MQTT_PASSWORD);
#else
  if (MQTT_USER != nullptr && MQTT_USER[0] != '\0') {
    ok = g_mqttClient.connect(clientId.c_str(), MQTT_USER, MQTT_PASSWORD);
  } else {
    ok = g_mqttClient.connect(clientId.c_str());
  }
#endif

  if (ok) {
    Serial.println("connected");
    g_mqttClient.subscribe(TOPIC_FILL_LIGHT);
    g_mqttClient.subscribe(TOPIC_VALVE);
    g_mqttClient.subscribe(TOPIC_COMMANDS);
    Serial.printf("MQTT: subscribed to %s, %s, %s\n",
                  TOPIC_FILL_LIGHT, TOPIC_VALVE, TOPIC_COMMANDS);
  } else {
    Serial.printf("failed (rc=%d), retry in %lu ms\n",
                  g_mqttClient.state(), MQTT_RETRY_MS);
  }
}

void mqttLoop() {
  g_mqttClient.loop();
}

bool mqttIsConnected() {
  return g_mqttClient.connected();
}

bool mqttPublish(const char* topic, const char* payload) {
  if (!g_mqttClient.connected()) {
    return false;
  }
  return g_mqttClient.publish(topic, payload);
}

PubSubClient* mqttClient() {
  return &g_mqttClient;
}
