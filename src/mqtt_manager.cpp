#include "mqtt_manager.h"

#include <WiFi.h>
#include "config.h"
#include "csv_logger.h"
#include "water_valve.h"
#include "wifi_manager.h"

static WiFiClient   g_wifiClient;
static PubSubClient g_mqttClient(g_wifiClient);
static unsigned long g_lastRetryMs = 0;

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

  if (!waterValveHandleCommand(message)) {
    csvLoggerHandleCommand(message);
  }
}

void mqttInit() {
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

  const unsigned long now = millis();
  if (now - g_lastRetryMs < MQTT_RETRY_MS) {
    return;
  }
  g_lastRetryMs = now;

  const String clientId = "ESP32S3Client-" + String(random(0, 1000));
  Serial.print("MQTT: connecting... ");

  if (g_mqttClient.connect(clientId.c_str())) {
    Serial.println("connected");
    g_mqttClient.subscribe(TOPIC_SUBSCRIBE);
    Serial.printf("MQTT: subscribed to %s\n", TOPIC_SUBSCRIBE);
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
