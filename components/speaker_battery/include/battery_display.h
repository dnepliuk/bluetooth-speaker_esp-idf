#pragma once

#include "battery_types.h"
#include "esp_err.h"

namespace BatteryDisplay {
// Starts an optional worker; physical I2C discovery/initialization is asynchronous.
esp_err_t begin(int sdaPin, int sclPin, uint8_t address);
// Replaces the sole pending snapshot without waiting for OLED I/O.
void publish(const SpeakerBattery::Reading& reading);
}  // namespace BatteryDisplay
