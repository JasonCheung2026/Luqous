#pragma once

#include <Arduino.h>

void waterValveInit();
void waterValveMaintain();
void waterValveMaintainAuto(uint16_t waterRaw);
bool waterValveIsOpen();
bool waterValveAutoEnabled();
bool waterValveHandleCommand(const String& message);
