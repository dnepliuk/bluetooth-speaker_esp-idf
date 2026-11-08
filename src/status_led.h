#pragma once

#include <cstdint>

#include "esp_err.h"

namespace StatusLed {

esp_err_t begin();
// Called only by the application loop; no independent timer drives the LED.
void update(int64_t nowUs);

}  // namespace StatusLed
