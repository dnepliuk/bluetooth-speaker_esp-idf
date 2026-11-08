#include "rotary_input.h"

#include <algorithm>
#include <cinttypes>
#include "app_config.h"
#include "bluetooth_audio.h"
#include "rotary_steps.h"
#include "driver/pulse_cnt.h"
#include "esp_check.h"

namespace {
constexpr char kTag[] = "ROTARY";
constexpr int kCountLimit = 30000;
pcnt_unit_handle_t unit = nullptr;
pcnt_channel_handle_t channel = nullptr;
bool enabled = false;
bool initialized = false;
RotarySteps steps(AppConfig::kEncoderCountsPerDetent, AppConfig::kEncoderReversed);
static_assert(AppConfig::kEncoderCountsPerDetent == 2);

esp_err_t configure() {
    pcnt_unit_config_t config{};
    config.low_limit = -kCountLimit;
    config.high_limit = kCountLimit;
    config.clk_src = PCNT_CLK_SRC_DEFAULT;
    config.flags.accum_count = true;
    ESP_RETURN_ON_ERROR(pcnt_new_unit(&config, &unit), kTag, "create unit");
    pcnt_glitch_filter_config_t filter{};
    filter.max_glitch_ns = AppConfig::kEncoderGlitchNs;
    ESP_RETURN_ON_ERROR(pcnt_unit_set_glitch_filter(unit, &filter), kTag, "set filter");

    pcnt_chan_config_t pins{};
    pins.edge_gpio_num = AppConfig::kEncoderAPin;
    pins.level_gpio_num = AppConfig::kEncoderBPin;
    ESP_RETURN_ON_ERROR(pcnt_new_channel(unit, &pins, &channel), kTag, "create channel");
    // Identical to ESP32Encoder::attachHalfQuad(A, B): A rising=-1,
    // A falling=+1, reverse while B is HIGH. No second counting channel.
    ESP_RETURN_ON_ERROR(pcnt_channel_set_edge_action(channel,
        PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE), kTag, "edges");
    ESP_RETURN_ON_ERROR(pcnt_channel_set_level_action(channel,
        PCNT_CHANNEL_LEVEL_ACTION_INVERSE, PCNT_CHANNEL_LEVEL_ACTION_KEEP), kTag, "levels");
    ESP_RETURN_ON_ERROR(gpio_set_pull_mode(AppConfig::kEncoderAPin, GPIO_PULLUP_ONLY), kTag, "A pullup");
    ESP_RETURN_ON_ERROR(gpio_set_pull_mode(AppConfig::kEncoderBPin, GPIO_PULLUP_ONLY), kTag, "B pullup");
    // Let the IDF driver's accumulator handle both hardware counter limits.
    ESP_RETURN_ON_ERROR(pcnt_unit_add_watch_point(unit, -kCountLimit), kTag, "low watch");
    ESP_RETURN_ON_ERROR(pcnt_unit_add_watch_point(unit, kCountLimit), kTag, "high watch");
    ESP_RETURN_ON_ERROR(pcnt_unit_enable(unit), kTag, "enable");
    enabled = true;
    ESP_RETURN_ON_ERROR(pcnt_unit_clear_count(unit), kTag, "clear");
    return pcnt_unit_start(unit);
}
}  // namespace

namespace RotaryInput {
esp_err_t begin() {
    if (initialized) return ESP_ERR_INVALID_STATE;
    const esp_err_t err = configure();
    if (err != ESP_OK) {
        if (enabled) { pcnt_unit_stop(unit); pcnt_unit_disable(unit); }
        if (channel) pcnt_del_channel(channel);
        if (unit) pcnt_del_unit(unit);
        channel = nullptr;
        unit = nullptr;
        enabled = false;
        return err;
    }
    initialized = true;
    ESP_LOGI(kTag, "A=GPIO%d B=GPIO%d; half-quad, %d counts/detent, filter=%" PRIu32
             " ns; volume step=%d reversed=%d",
             static_cast<int>(AppConfig::kEncoderAPin), static_cast<int>(AppConfig::kEncoderBPin),
             AppConfig::kEncoderCountsPerDetent, AppConfig::kEncoderGlitchNs,
             AppConfig::kVolumeStep, AppConfig::kEncoderReversed);
    return ESP_OK;
}

void update() {
    if (!initialized) return;
    int count = 0;
    ESP_ERROR_CHECK(pcnt_unit_get_count(unit, &count));
    const int64_t detents = steps.take(count);
    if (detents == 0) return;
    // Equivalent to repeated +/-4 with saturation; work stays bounded even
    // after a large accumulated movement. Disconnected movement is consumed.
    const int delta = static_cast<int>(std::clamp<int64_t>(
        detents * AppConfig::kVolumeStep, -127, 127));
    BluetoothAudio::adjustVolume(delta);
}
}  // namespace RotaryInput
