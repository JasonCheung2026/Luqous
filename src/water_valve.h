#pragma once

#include <Arduino.h>

void waterValveInit();
void waterValveMaintain();
bool waterValveIsOpen();
bool waterValveHandleMqtt(const String& message);
