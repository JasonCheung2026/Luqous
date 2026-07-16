#pragma once

#include <Arduino.h>

void bh1750Init();
bool bh1750Present();
uint16_t bh1750ReadLux();
