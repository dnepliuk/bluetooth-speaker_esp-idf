#include "audio_output.h"

#include <cinttypes>
#include "esp_log.h"

namespace {
constexpr char kTag[] = "AUDIO";
}

esp_err_t IdfI2sOutput::preloadSilence() {
    // Fill every DMA descriptor while the channel is disabled, so a new rate
    // never starts by replaying PCM left over from the previous stream.
    static const uint8_t silence[kDmaFrames * 4] = {};
    for (uint32_t i = 0; i < kDmaDescriptors; ++i) {
        size_t loaded = 0;
        const esp_err_t err = i2s_channel_preload_data(
            channel_, silence, sizeof(silence), &loaded);
        if (err != ESP_OK) return err;
        if (loaded != sizeof(silence)) return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

esp_err_t IdfI2sOutput::initialize(const BluetoothAudio::Config& config) {
    if (channel_ != nullptr) return ESP_ERR_INVALID_STATE;
    mutex_ = xSemaphoreCreateMutexStatic(&mutexStorage_);
    if (mutex_ == nullptr) return ESP_ERR_NO_MEM;

    i2s_chan_config_t channelConfig =
        I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channelConfig.dma_desc_num = kDmaDescriptors;
    channelConfig.dma_frame_num = kDmaFrames;
    channelConfig.auto_clear_after_cb = true;
    esp_err_t err = i2s_new_channel(&channelConfig, &channel_, nullptr);
    if (err != ESP_OK) return err;

    i2s_std_config_t standard{};
    standard.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(config.initialSampleRate);
    standard.clk_cfg.clk_src = I2S_CLK_SRC_APLL;
    standard.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
        I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    standard.gpio_cfg.mclk = I2S_GPIO_UNUSED;
    standard.gpio_cfg.bclk = static_cast<gpio_num_t>(config.bckPin);
    standard.gpio_cfg.ws = static_cast<gpio_num_t>(config.wsPin);
    standard.gpio_cfg.dout = static_cast<gpio_num_t>(config.dataPin);
    standard.gpio_cfg.din = I2S_GPIO_UNUSED;
    err = i2s_channel_init_std_mode(channel_, &standard);
    if (err == ESP_OK) err = preloadSilence();
    if (err == ESP_OK) err = i2s_channel_enable(channel_);
    if (err != ESP_OK) {
        i2s_del_channel(channel_);
        channel_ = nullptr;
        return err;
    }
    running_ = true;
    accepting_ = true;  // Matches the library's initial is_i2s_active state.
    sampleRate_.store(config.initialSampleRate);
    ESP_LOGI(kTag, "I2S ready: BCK=%d WS=%d DATA=%d; %" PRIu32
             " Hz, s16 stereo Philips, APLL", config.bckPin, config.wsPin,
             config.dataPin, config.initialSampleRate);
    return ESP_OK;
}

size_t IdfI2sOutput::write(const uint8_t* data, size_t size) {
    if (size == 0) return 0;
    if (data == nullptr || size % 4 != 0 || mutex_ == nullptr) {
        droppedBytes_.fetch_add(size);
        writeErrors_.fetch_add(1);
        return 0;
    }
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(kWriteTimeoutMs)) != pdTRUE) {
        droppedBytes_.fetch_add(size);
        writeErrors_.fetch_add(1);
        return 0;
    }
    size_t written = 0;
    if (running_ && accepting_) {
        const esp_err_t err = i2s_channel_write(
            channel_, data, size, &written, kWriteTimeoutMs);
        if (err != ESP_OK || written != size) writeErrors_.fetch_add(1);
    }
    xSemaphoreGive(mutex_);
    bytesWritten_.fetch_add(written);
    droppedBytes_.fetch_add(size - written);
    return written;
}

void IdfI2sOutput::set_sample_rate(int rate) {
    if (rate != 16000 && rate != 32000 && rate != 44100 && rate != 48000) {
        ESP_LOGE(kTag, "Unsupported sample rate: %d", rate);
        ESP_ERROR_CHECK(ESP_ERR_NOT_SUPPORTED);
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (sampleRate_.load() != static_cast<uint32_t>(rate)) {
        // Serialize reconfiguration against the PCM callback's DMA writes.
        ESP_ERROR_CHECK(i2s_channel_disable(channel_));
        running_ = false;
        i2s_std_clk_config_t clock =
            I2S_STD_CLK_DEFAULT_CONFIG(static_cast<uint32_t>(rate));
        clock.clk_src = I2S_CLK_SRC_APLL;
        ESP_ERROR_CHECK(i2s_channel_reconfig_std_clock(channel_, &clock));
        ESP_ERROR_CHECK(preloadSilence());
        ESP_ERROR_CHECK(i2s_channel_enable(channel_));
        running_ = true;
        sampleRate_.store(rate);
    }
    xSemaphoreGive(mutex_);
}

void IdfI2sOutput::set_output_active(bool active) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    accepting_ = active;
    // Keep BCK/WS running. Auto-clear sends silence once queued PCM drains,
    // including after pause/disconnect, rather than repeating the last block.
    xSemaphoreGive(mutex_);
}

void IdfI2sOutput::end() {
    if (mutex_ == nullptr) return;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    accepting_ = false;
    if (channel_ != nullptr) {
        if (running_) ESP_ERROR_CHECK(i2s_channel_disable(channel_));
        ESP_ERROR_CHECK(i2s_del_channel(channel_));
        channel_ = nullptr;
        running_ = false;
    }
    xSemaphoreGive(mutex_);
}

void IdfI2sOutput::logStats() const {
    ESP_LOGI(kTag, "PCM: rate=%" PRIu32 " Hz written=%" PRIu32
             " dropped=%" PRIu32 " write_errors=%" PRIu32,
             sampleRate_.load(), bytesWritten_.load(),
             droppedBytes_.load(), writeErrors_.load());
}
