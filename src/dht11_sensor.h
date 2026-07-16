#pragma once

#include <Arduino.h>

void dht11Init();
bool dht11Present();
bool dht11Read(float* temperature, float* humidity);
