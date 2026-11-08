#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include "BluetoothA2DPOutput.h"
#include "bluetooth_audio.h"
#include "driver/i2s_std.h"
#include "freertos/semphr.h"

class IdfI2sOutput final : public BluetoothA2DPOutput {
public:
    esp_err_t initialize(const BluetoothAudio::Config& config);
    bool begin() override { return channel_ != nullptr; }
    size_t write(const uint8_t* data, size_t size) override;
    void end() override;
    void set_sample_rate(int rate) override;
    void set_output_active(bool active) override;

    uint32_t sampleRate() const { return sampleRate_.load(); }
    void logStats() const;

private:
    esp_err_t preloadSilence();
    static constexpr uint32_t kDmaDescriptors = 6;
    static constexpr uint32_t kDmaFrames = 240;
    static constexpr uint32_t kWriteTimeoutMs = 100;
    i2s_chan_handle_t channel_ = nullptr;
    StaticSemaphore_t mutexStorage_{};
    SemaphoreHandle_t mutex_ = nullptr;
    bool running_ = false;  // Protected by mutex_; I2S clocks run during pause.
    bool accepting_ = false;
    std::atomic<uint32_t> sampleRate_{0};
    std::atomic<uint32_t> bytesWritten_{0};
    std::atomic<uint32_t> droppedBytes_{0};
    std::atomic<uint32_t> writeErrors_{0};
};
