#include <cinttypes>

#include "app_config.h"
#include "bluetooth_audio.h"
#include "pairing_button.h"
#include "status_led.h"

#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

namespace {

constexpr char kTag[] = "SPEAKER";
constexpr TickType_t kServiceTicks = pdMS_TO_TICKS(AppConfig::kServiceIntervalMs);
static_assert(kServiceTicks > 0, "The service interval must span at least one tick");
static_assert(CONFIG_ESP_CONSOLE_UART_BAUDRATE == AppConfig::kSerialBaud);

const char* resetReasonName(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_UNKNOWN: return "unknown";
        case ESP_RST_POWERON: return "power-on";
        case ESP_RST_EXT: return "external";
        case ESP_RST_SW: return "software";
        case ESP_RST_PANIC: return "panic";
        case ESP_RST_INT_WDT: return "interrupt-watchdog";
        case ESP_RST_TASK_WDT: return "task-watchdog";
        case ESP_RST_WDT: return "watchdog";
        case ESP_RST_DEEPSLEEP: return "deep-sleep";
        case ESP_RST_BROWNOUT: return "brownout";
        case ESP_RST_SDIO: return "sdio";
        case ESP_RST_USB: return "usb";
        case ESP_RST_JTAG: return "jtag";
        case ESP_RST_EFUSE: return "efuse";
        case ESP_RST_PWR_GLITCH: return "power-glitch";
        case ESP_RST_CPU_LOCKUP: return "cpu-lockup";
        default: return "unrecognized";
    }
}

void initializeIdleStripPin() {
    // Set the output latch before enabling the level-shifter input driver.
    ESP_ERROR_CHECK(gpio_set_level(AppConfig::kLedStripPin, 0));
    gpio_config_t config{};
    config.pin_bit_mask = 1ULL << AppConfig::kLedStripPin;
    config.mode = GPIO_MODE_OUTPUT;
    config.pull_up_en = GPIO_PULLUP_DISABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&config));
}

void logStartup() {
    const esp_app_desc_t* app = esp_app_get_description();
    const esp_reset_reason_t reason = esp_reset_reason();
    esp_chip_info_t chip{};
    esp_chip_info(&chip);

    ESP_LOGI(kTag, "FAITAL_SPEAKER firmware=esp-idf version=%s stage=3", app->version);
    ESP_LOGI(kTag, "ESP-IDF=%s target=%s cores=%d revision=%u.%u",
             esp_get_idf_version(), CONFIG_IDF_TARGET, chip.cores,
             static_cast<unsigned>(chip.revision / 100),
             static_cast<unsigned>(chip.revision % 100));
    ESP_LOGI(kTag, "Reset reason: %s (%d)", resetReasonName(reason),
             static_cast<int>(reason));

    uint32_t physicalFlashBytes = 0;
    ESP_ERROR_CHECK(esp_flash_get_physical_size(nullptr, &physicalFlashBytes));
    ESP_LOGI(kTag, "Flash: configured=%" PRIu32 " bytes detected=%" PRIu32 " bytes",
             AppConfig::kExpectedFlashBytes, physicalFlashBytes);
    if (physicalFlashBytes < AppConfig::kExpectedFlashBytes) {
        ESP_LOGE(kTag, "Detected flash is smaller than the configured layout");
        ESP_ERROR_CHECK(ESP_ERR_INVALID_SIZE);
    }

    const esp_partition_t* appPartition = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, nullptr);
    if (appPartition == nullptr ||
        appPartition->size != AppConfig::kApplicationPartitionBytes) {
        ESP_LOGE(kTag, "Expected a 3 MiB factory application partition");
        ESP_ERROR_CHECK(ESP_ERR_INVALID_SIZE);
    }
    ESP_LOGI(kTag, "App partition: %s offset=0x%" PRIx32 " size=%" PRIu32 " bytes",
             appPartition->label, appPartition->address, appPartition->size);
    ESP_LOGI(kTag, "Heap: free=%" PRIu32 " bytes minimum=%" PRIu32 " bytes",
             esp_get_free_heap_size(), esp_get_minimum_free_heap_size());
    ESP_LOGI(kTag, "STATUS LED: GPIO=%d period=%" PRIu32 " ms pulse=%" PRIu32 " ms",
             static_cast<int>(AppConfig::kStatusLedPin),
             AppConfig::kStatusLedHeartbeatPeriodMs,
             AppConfig::kStatusLedHeartbeatOnMs);
}

}  // namespace

extern "C" void app_main(void) {
    initializeIdleStripPin();
    logStartup();
    ESP_ERROR_CHECK(StatusLed::begin());
    const BluetoothAudio::Config audioConfig{
        AppConfig::kBluetoothName,
        AppConfig::kI2sBckPin, AppConfig::kI2sWsPin, AppConfig::kI2sDataPin,
        AppConfig::kInitialSampleRate, AppConfig::kVolumeFallback,
        AppConfig::kReconnectAttempts};
    ESP_ERROR_CHECK(BluetoothAudio::begin(audioConfig));
    ESP_ERROR_CHECK(PairingButton::begin());
    ESP_LOGI(kTag, "Stage 3 started: application loop=%" PRIu32 " ms; reconnect/pairing button",
             AppConfig::kServiceIntervalMs);

    TickType_t lastWake = xTaskGetTickCount();
    int64_t lastLogUs = esp_timer_get_time();
    while (true) {
        const int64_t nowUs = esp_timer_get_time();
        StatusLed::update(nowUs);
        PairingButton::update();
        BluetoothAudio::update();

        if (nowUs - lastLogUs >= AppConfig::kRuntimeLogIntervalUs) {
            lastLogUs = nowUs;
            ESP_LOGI(kTag, "Loop alive: uptime=%" PRId64 " s free_heap=%" PRIu32
                     " minimum_heap=%" PRIu32,
                     nowUs / 1000000, esp_get_free_heap_size(),
                     esp_get_minimum_free_heap_size());
            BluetoothAudio::logStats();
        }

        // Block until the next service slot so idle tasks and watchdogs can run.
        xTaskDelayUntil(&lastWake, kServiceTicks);
    }
}
