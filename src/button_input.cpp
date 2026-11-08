#include "button_input.h"

#include <atomic>
#include "app_config.h"
#include "bluetooth_audio.h"
#include "button_gpio.h"
#include "iot_button.h"
#include "esp_log.h"
#include "sdkconfig.h"

namespace {
constexpr char kTag[] = "BUTTONS";
enum Button : unsigned { Pairing, PlayPause, Previous, Next, Count };
struct Input {
    gpio_num_t pin;
    const char* name;
    unsigned bit;
    button_handle_t handle = nullptr;
};
Input inputs[] = {
    {AppConfig::kBluetoothButtonPin, "Bluetooth", 1U << Pairing},
    {AppConfig::kEncoderSwitchPin, "Play/Pause", 1U << PlayPause},
    {AppConfig::kPreviousButtonPin, "Previous", 1U << Previous},
    {AppConfig::kNextButtonPin, "Next", 1U << Next},
};
std::atomic<unsigned> clicks{0};
bool initialized = false;
static_assert(CONFIG_BUTTON_PERIOD_TIME_MS * CONFIG_BUTTON_DEBOUNCE_TICKS ==
              AppConfig::kButtonDebounceMs, "Button debounce must match the reference");

void onRelease(void* handle, void* user) {
    // Same short-release semantics as stage 3 and OneButton attachClick:
    // debounce is supplied by the component; no multi-click delay or hold repeat.
    if (iot_button_get_pressed_time(static_cast<button_handle_t>(handle)) <=
        AppConfig::kButtonLongPressMs) {
        clicks.fetch_or(static_cast<Input*>(user)->bit);
    }
}
}  // namespace

namespace ButtonInput {
esp_err_t begin() {
    if (initialized) return ESP_ERR_INVALID_STATE;
    for (auto& input : inputs) {
        button_config_t config{};
        config.long_press_time = AppConfig::kButtonLongPressMs;
        button_gpio_config_t gpio{};
        gpio.gpio_num = input.pin;
        gpio.active_level = 0;
        gpio.enable_power_save = false;
        gpio.disable_pull = false;
        esp_err_t err = iot_button_new_gpio_device(&config, &gpio, &input.handle);
        if (err == ESP_OK) {
            err = iot_button_register_cb(input.handle, BUTTON_PRESS_UP, nullptr,
                                        onRelease, &input);
        }
        if (err != ESP_OK) {
            for (auto& created : inputs) {
                if (created.handle) iot_button_delete(created.handle);
                created.handle = nullptr;
            }
            clicks.store(0);
            return err;
        }
        ESP_LOGI(kTag, "%s=GPIO%d active-low pullup=internal debounce=%u ms; short release",
                 input.name, static_cast<int>(input.pin), AppConfig::kButtonDebounceMs);
    }
    initialized = true;
    return ESP_OK;
}

void update() {
    const unsigned pending = clicks.exchange(0);
    if (pending & (1U << Pairing)) {
        ESP_LOGI(kTag, "Bluetooth button clicked");
        BluetoothAudio::enterPairingMode();
        return;  // Pairing takes priority over simultaneous media clicks.
    }
    using BluetoothAudio::MediaCommand;
    if (pending & (1U << PlayPause)) BluetoothAudio::requestMedia(MediaCommand::TogglePlayPause);
    if (pending & (1U << Previous)) BluetoothAudio::requestMedia(MediaCommand::Previous);
    if (pending & (1U << Next)) BluetoothAudio::requestMedia(MediaCommand::Next);
}
}  // namespace ButtonInput
