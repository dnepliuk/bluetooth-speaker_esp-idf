#pragma once

#include "esp_err.h"
#include "strip_animation.h"

namespace StatusStrip {
// Starts the sole owner of the native RMT strip on core 1, before Bluetooth.
// Failure is optional: the caller must continue starting audio and inputs.
esp_err_t begin(int gpio, uint8_t brightness);
// Main-loop producer; one latest state, zero wait, no shared pixel buffer.
void setState(StripAnimation::State state);
}  // namespace StatusStrip
