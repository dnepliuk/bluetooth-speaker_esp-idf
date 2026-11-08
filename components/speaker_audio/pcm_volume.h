#pragma once

#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include "A2DPVolumeControl.h"

// Retain the upstream nonlinear gain and integer sample rounding exactly.
// AVRCP publishes only the requested level; the PCM task owns the calculator.
class PcmVolume final : public A2DPVolumeControl {
public:
    void set_volume(uint8_t volume) override {
        requested_.store(volume > 127 ? 127 : volume, std::memory_order_relaxed);
    }

    void update_audio_data(Frame* data, uint16_t frames) override {
        const uint8_t requested = requested_.load(std::memory_order_relaxed);
        if (requested != applied_) {
            curve_.set_volume(requested);
            applied_ = requested;
        }
        curve_.update_audio_data(data, frames);
    }

private:
    class Curve : public A2DPDefaultVolumeControl {
    public:
        Curve() { set_enabled(true); }
        using A2DPDefaultVolumeControl::set_volume;
    };
    std::atomic<uint8_t> requested_{0};
    uint8_t applied_ = 255;
    Curve curve_;
};
