#include "pairing_button.h"

#include <atomic>
#include "app_config.h"
#include "bluetooth_audio.h"
#include "button_gpio.h"
#include "iot_button.h"
#include "esp_log.h"
#include "sdkconfig.h"

namespace {
constexpr char kTag[] = "BUTTONS";
button_handle_t button = nullptr;
std::atomic<bool> clickPending{false};
static_assert(CONFIG_BUTTON_PERIOD_TIME_MS * CONFIG_BUTTON_DEBOUNCE_TICKS ==
              AppConfig::kButtonDebounceMs, "Button debounce must match the reference");

void onRelease(void* handle, void*) {
    // PRESS_UP is already debounced. OneButton with only attachClick does not
    // wait for a double-click; ignore long holds as in its default 800 ms limit.
    // Using SINGLE_CLICK here would add the native library's multi-click delay.
    if (iot_button_get_pressed_time(static_cast<button_handle_t>(handle)) <=
        AppConfig::kButtonLongPressMs) {
        clickPending.store(true);
    }
}
}  // namespace

namespace PairingButton {
esp_err_t begin() {
    if (button != nullptr) return ESP_ERR_INVALID_STATE;
    button_config_t config{};
    config.long_press_time = AppConfig::kButtonLongPressMs;
    button_gpio_config_t gpio{};
    gpio.gpio_num = AppConfig::kBluetoothButtonPin;
    gpio.active_level = 0;
    gpio.enable_power_save = false;
    gpio.disable_pull = false;
    esp_err_t err = iot_button_new_gpio_device(&config, &gpio, &button);
    if (err != ESP_OK) return err;
    err = iot_button_register_cb(button, BUTTON_PRESS_UP, nullptr, onRelease, nullptr);
    if (err != ESP_OK) {
        iot_button_delete(button);
        button = nullptr;
        return err;
    }
    ESP_LOGI(kTag, "Bluetooth=GPIO%d active-low pullup=internal debounce=%u ms; short release",
             static_cast<int>(AppConfig::kBluetoothButtonPin), AppConfig::kButtonDebounceMs);
    return ESP_OK;
}

void update() {
    if (clickPending.exchange(false)) {
        ESP_LOGI(kTag, "Bluetooth button clicked");
        BluetoothAudio::enterPairingMode();
    }
}
}  // namespace PairingButton
