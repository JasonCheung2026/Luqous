#pragma once

#include <Arduino.h>
#include <PubSubClient.h>

void mqttInit();
void mqttMaintain();
void mqttLoop();
bool mqttIsConnected();
bool mqttPublish(const char* topic, const char* payload);
PubSubClient* mqttClient();
