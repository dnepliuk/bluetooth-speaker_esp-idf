#include "status_strip.h"

#include <atomic>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "led_strip.h"

namespace {
constexpr char kTag[] = "STRIP";
constexpr size_t kRmtSymbols = 256;
// Eight GRB pixels + reset + EOF fit before TX starts: no mid-frame refill.
static_assert(StripAnimation::kPixelCount * 24 + 2 <= kRmtSymbols);
static_assert(pdMS_TO_TICKS(10) > 0);
QueueHandle_t states = nullptr;
std::atomic<bool> accepting{false};
struct Wiring { int gpio; uint8_t brightness; } wiring;
StripAnimation::State published = StripAnimation::State::Initializing; // Main loop only.

const char* stateName(StripAnimation::State state) {
    switch (state) {
        case StripAnimation::State::Initializing: return "initializing";
        case StripAnimation::State::Waiting: return "waiting";
        case StripAnimation::State::ConnectedIdle: return "connected-idle";
        case StripAnimation::State::Playing: return "playing";
    }
    return "unknown";
}

esp_err_t initialize(led_strip_handle_t* strip) {
    led_strip_config_t config{};
    config.strip_gpio_num = wiring.gpio;
    config.max_leds = StripAnimation::kPixelCount;
    config.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;
    // Reuse the component's public timing configuration with the working
    // NeoPixel ESP32 800 kHz profile. 20 MHz represents every 50 ns exactly.
    config.led_model = LED_MODEL_CUSTOM;
    config.timings = {400, 800, 850, 450, 300}; // T0H, T1H, T0L, T1L, reset(us).
    led_strip_rmt_config_t rmt{};
    rmt.clk_src = RMT_CLK_SRC_DEFAULT;
    rmt.resolution_hz = 20000000;
    rmt.mem_block_symbols = kRmtSymbols;
    rmt.flags.with_dma = false; // Classic ESP32 has no RMT DMA.
    const esp_err_t err = led_strip_new_rmt_device(&config, &rmt, strip);
    if (err != ESP_OK) return err;
    return led_strip_clear(*strip);
}

esp_err_t show(led_strip_handle_t strip, const StripAnimation::Frame& frame) {
    for (size_t i = 0; i < frame.size(); ++i) {
        const auto& pixel = frame[i];
        const esp_err_t err = led_strip_set_pixel(strip, i, pixel.red, pixel.green, pixel.blue);
        if (err != ESP_OK) return err;
    }
    // The library waits for hardware completion only in this worker. Never
    // call it from the main loop, GPIO callbacks or Bluetooth/audio tasks.
    return led_strip_refresh(strip);
}

void worker(void*) {
    led_strip_handle_t strip = nullptr;
    esp_err_t err = initialize(&strip);
    StripAnimation::Animator animator(esp_timer_get_time() / 1000, wiring.brightness);
    StripAnimation::Frame frame{};
    if (err == ESP_OK) {
        ESP_LOGI(kTag, "WS2812 GPIO=%d pixels=%u GRB=800kHz brightness=%u/255; RMT=%u symbols core=1",
                 wiring.gpio, static_cast<unsigned>(StripAnimation::kPixelCount),
                 wiring.brightness, static_cast<unsigned>(kRmtSymbols));
        ESP_LOGI(kTag, "State: initializing");
    }
    while (err == ESP_OK) {
        auto next = animator.state();
        xQueueReceive(states, &next, pdMS_TO_TICKS(10));
        const uint64_t nowMs = esp_timer_get_time() / 1000;
        if (animator.setState(next, nowMs)) ESP_LOGI(kTag, "State: %s", stateName(next));
        if (animator.update(nowMs, frame)) err = show(strip, frame);
    }
    accepting.store(false);
    ESP_LOGW(kTag, "WS2812 unavailable: %s; continuing without strip until restart", esp_err_to_name(err));
    if (strip) led_strip_del(strip);
    // Keep this single-slot queue alive: a publisher may have passed the flag
    // check. No task outside this worker ever touches the driver or its buffer.
    vTaskDelete(nullptr);
}
}  // namespace

namespace StatusStrip {
esp_err_t begin(int gpio, uint8_t brightness) {
    if (states) return ESP_ERR_INVALID_STATE;
    wiring = {gpio, brightness};
    states = xQueueCreate(1, sizeof(StripAnimation::State));
    if (!states) return ESP_ERR_NO_MEM;
    accepting.store(true);
    if (xTaskCreatePinnedToCore(worker, "status_strip", 3072, nullptr, 1, nullptr, 1) != pdPASS) {
        accepting.store(false);
        vQueueDelete(states);
        states = nullptr;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void setState(StripAnimation::State state) {
    if (accepting.load() && state != published) {
        xQueueOverwrite(states, &state);
        published = state;
    }
}
}  // namespace StatusStrip
