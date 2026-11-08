#pragma once

#include "esp_err.h"

namespace ButtonInput {
esp_err_t begin();
void update();
}
