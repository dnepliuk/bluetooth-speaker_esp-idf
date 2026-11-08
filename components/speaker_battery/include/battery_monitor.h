#pragma once

#include "battery_types.h"
#include "esp_err.h"

namespace BatteryMonitor {
// Main application task owns initialization, sampling and snapshots.
esp_err_t begin(int adcPin, SpeakerBattery::Calibration calibration);
// At most one conversion; true only when a new snapshot is published.
bool update(int64_t nowUs);
SpeakerBattery::Reading reading();
}  // namespace BatteryMonitor
