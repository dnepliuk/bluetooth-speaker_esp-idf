#include "battery_display.h"

#include <atomic>
#include <cstdio>
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "ssd1306.h"
#include "fonts/adafruit/glcdfont.c"

// Only the Registry driver's I2C calls are redirected by its target's compile
// definition. Upstream sources stay intact. Never allow its -1 infinite wait.
extern "C" esp_err_t speaker_oled_i2c_transmit(i2c_master_dev_handle_t device,
    const uint8_t* data, size_t size, int timeoutMs) {
    return i2c_master_transmit(device, data, size,
                               timeoutMs < 0 || timeoutMs > 20 ? 20 : timeoutMs);
}

namespace {
constexpr char kTag[] = "DISPLAY";
QueueHandle_t readings = nullptr;
std::atomic<bool> accepting{false};
struct Wiring { int sda; int scl; uint8_t address; } wiring;
const ssd1306_font_t referenceFont{5, 8, 0, 255, font};
static_assert(sizeof(font) == 256 * 5);

// Preserve Adafruit's 6*scale character advance; this library otherwise uses
// 5*scale+1. Glyph rendering and clipping remain entirely in the library.
esp_err_t text(ssd1306_handle_t display, int x, int y, const char* value, int scale) {
    for (; *value; ++value, x += 6 * scale) {
        const char glyph[]{*value, '\0'};
        const esp_err_t err = ssd1306_draw_text_scaled(display, x, y, glyph, true, scale);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

esp_err_t render(ssd1306_handle_t display, const SpeakerBattery::Reading& value, bool blinkOn) {
    esp_err_t err = ssd1306_clear(display);
    if (err != ESP_OK) return err;
    if (!value.valid) {
        if ((err = text(display, 21, 8, "BATTERY", 2)) != ESP_OK) return err;
        if ((err = text(display, 34, 29, "ERROR", 2)) != ESP_OK) return err;
        if ((err = text(display, 46, 53, "--.- V", 1)) != ESP_OK) return err;
    } else {
        char percent[6];
        char voltage[12];
        std::snprintf(percent, sizeof(percent), "%u%%", value.percent);
        std::snprintf(voltage, sizeof(voltage), "%.2f V", static_cast<double>(value.voltage));
        if ((err = text(display, 0, 2, percent, 3)) != ESP_OK) return err;
        if (!value.low || blinkOn) {
            if ((err = ssd1306_draw_rect(display, 91, 7, 32, 18, false)) != ESP_OK) return err;
            if ((err = ssd1306_draw_rect(display, 123, 12, 3, 8, true)) != ESP_OK) return err;
            const int fill = value.percent * 28 / 100;
            if (fill && (err = ssd1306_draw_rect(display, 93, 9, fill, 14, true)) != ESP_OK) return err;
        }
        if ((err = text(display, 0, 31, voltage, 2)) != ESP_OK) return err;
        if (value.low && (err = text(display, 29, 55, "LOW BATTERY", 1)) != ESP_OK) return err;
    }
    return ssd1306_display(display);
}

esp_err_t initialize(i2c_master_bus_handle_t* bus, ssd1306_handle_t* display) {
    i2c_master_bus_config_t busConfig{};
    busConfig.i2c_port = I2C_NUM_0;
    busConfig.sda_io_num = static_cast<gpio_num_t>(wiring.sda);
    busConfig.scl_io_num = static_cast<gpio_num_t>(wiring.scl);
    busConfig.clk_source = I2C_CLK_SRC_DEFAULT;
    busConfig.glitch_ignore_cnt = 7;
    busConfig.flags.enable_internal_pullup = true;
    esp_err_t err = i2c_new_master_bus(&busConfig, bus);
    if (err != ESP_OK) return err;
    err = i2c_master_probe(*bus, wiring.address, 50);
    if (err != ESP_OK) return err;
    ssd1306_config_t config{};
    config.bus = SSD1306_I2C;
    config.width = 128;
    config.height = 64;
    config.iface.i2c.port = I2C_NUM_0;
    config.iface.i2c.addr = wiring.address;
    config.iface.i2c.rst_gpio = GPIO_NUM_NC;
    err = ssd1306_new_i2c(&config, display);
    if (err != ESP_OK) return err;
    return ssd1306_set_font(*display, &referenceFont);
}

void worker(void*) {
    ESP_LOGI(kTag, "SSD1306 128x64 address=0x%02x SDA=%d SCL=%d; I2C=400000 Hz",
             wiring.address, wiring.sda, wiring.scl);
    i2c_master_bus_handle_t bus = nullptr;
    ssd1306_handle_t display = nullptr;
    esp_err_t err = initialize(&bus, &display);
    SpeakerBattery::Reading value{};
    SpeakerBattery::Reading rendered{};
    bool hasRendered = false;
    bool lastBlinkOn = true;
    int64_t lastRenderUs = 0;
    if (err == ESP_OK) ESP_LOGI(kTag, "Initialized; battery display worker ready");
    while (err == ESP_OK) {
        // One latest snapshot; no accumulating display work or shared framebuffer.
        xQueueReceive(readings, &value, pdMS_TO_TICKS(50));
        const int64_t now = esp_timer_get_time();
        const bool blinkOn = !value.low || (now / SpeakerBattery::kBlinkIntervalUs) % 2 == 0;
        const bool changed = !hasRendered || value.valid != rendered.valid ||
            value.low != rendered.low || value.percent != rendered.percent;
        if (changed || (value.low && blinkOn != lastBlinkOn) ||
            now - lastRenderUs >= SpeakerBattery::kDisplayIntervalUs) {
            err = render(display, value, blinkOn);
            rendered = value;
            lastBlinkOn = blinkOn;
            lastRenderUs = now;
            hasRendered = true;
        }
    }
    accepting.store(false);
    ESP_LOGW(kTag, "OLED unavailable: %s; continuing without display until restart", esp_err_to_name(err));
    // The driver may return a partly initialized handle; its public destructor
    // releases that allocation too. This task exclusively owns all I2C objects.
    if (display) ssd1306_del(display);
    if (bus) i2c_del_master_bus(bus);
    // Keep the single-slot queue allocated: a main-loop publisher may already
    // have passed the accepting check. It cannot access a deleted queue.
    vTaskDelete(nullptr);
}
}  // namespace

namespace BatteryDisplay {
esp_err_t begin(int sdaPin, int sclPin, uint8_t address) {
    if (readings) return ESP_ERR_INVALID_STATE;
    wiring = {sdaPin, sclPin, address};
    readings = xQueueCreate(1, sizeof(SpeakerBattery::Reading));
    if (!readings) return ESP_ERR_NO_MEM;
    accepting.store(true);
    if (xTaskCreate(worker, "battery_oled", 4096, nullptr, 1, nullptr) != pdPASS) {
        accepting.store(false);
        vQueueDelete(readings);
        readings = nullptr;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void publish(const SpeakerBattery::Reading& value) {
    if (accepting.load()) xQueueOverwrite(readings, &value);
}
}  // namespace BatteryDisplay
