#pragma once

#include "oled_display.h"

void telemetryPrimeCache();
void telemetryCollectAndPublish();
void telemetryFillSnapshot(DisplaySnapshot* out);
