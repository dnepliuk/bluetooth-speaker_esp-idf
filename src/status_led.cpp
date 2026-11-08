#include "status_led.h"

#include "app_config.h"
#include "esp_timer.h"

namespace StatusLed {
namespace {

int64_t s_startedAtUs = 0;
bool s_outputOn = false;

int outputLevel(bool on) {
    return (on == AppConfig::kStatusLedActiveHigh) ? 1 : 0;
}

}  // namespace

esp_err_t begin() {
    esp_err_t error = gpio_set_level(AppConfig::kStatusLedPin, outputLevel(false));
    if (error != ESP_OK) {
        return error;
    }

    gpio_config_t config{};
    config.pin_bit_mask = 1ULL << AppConfig::kStatusLedPin;
    config.mode = GPIO_MODE_OUTPUT;
    config.pull_up_en = GPIO_PULLUP_DISABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;
    error = gpio_config(&config);
    if (error != ESP_OK) {
        return error;
    }

    s_startedAtUs = esp_timer_get_time();
    s_outputOn = false;
    return ESP_OK;
}

void update(int64_t nowUs) {
    constexpr int64_t kPeriodUs =
        static_cast<int64_t>(AppConfig::kStatusLedHeartbeatPeriodMs) * 1000;
    constexpr int64_t kOnUs =
        static_cast<int64_t>(AppConfig::kStatusLedHeartbeatOnMs) * 1000;
    const bool on = (nowUs - s_startedAtUs) % kPeriodUs < kOnUs;
    if (on != s_outputOn) {
        ESP_ERROR_CHECK(gpio_set_level(AppConfig::kStatusLedPin, outputLevel(on)));
        s_outputOn = on;
    }
}

}  // namespace StatusLed
