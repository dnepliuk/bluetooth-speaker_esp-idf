#pragma once

#include <algorithm>
#include <array>
#include "battery_types.h"

namespace SpeakerBattery {
using Samples = std::array<uint32_t, kSampleCount>;
static_assert(kSampleCount >= 16 && 2 * kTrimPerEnd < kSampleCount);

constexpr uint32_t trimmedMean(Samples samples) {
    std::sort(samples.begin(), samples.end());
    uint64_t sum = 0;
    for (unsigned i = kTrimPerEnd; i < kSampleCount - kTrimPerEnd; ++i) sum += samples[i];
    constexpr unsigned retained = kSampleCount - 2 * kTrimPerEnd;
    return static_cast<uint32_t>((sum + retained / 2) / retained);
}

constexpr float packVoltage(uint32_t mv, Calibration calibration) {
    return (mv / 1000.0f) *
        ((static_cast<float>(calibration.highOhm) + calibration.lowOhm) / calibration.lowOhm) *
        calibration.gain + calibration.offsetV;
}

constexpr bool validMeasurement(uint32_t mv, float voltage) {
    // Ordered range checks also reject NaN and infinities.
    return mv > 0 && mv <= kAdcMaxMillivolts && voltage >= kValidMinV && voltage <= kValidMaxV;
}

constexpr bool lowState(bool wasLow, float voltage) {
    return wasLow ? voltage < kLowClearV : voltage <= kLowEnterV;
}

constexpr uint8_t voltageToPercent(float voltage) {
    struct Point { float voltage; uint8_t percent; };
    constexpr Point curve[] = {
        {10.20f, 0}, {10.80f, 10}, {11.10f, 20}, {11.40f, 35},
        {11.70f, 55}, {12.00f, 75}, {12.30f, 90}, {12.60f, 100}};
    if (voltage <= curve[0].voltage) return 0;
    for (unsigned i = 1; i < std::size(curve); ++i) {
        if (voltage <= curve[i].voltage) {
            const auto& lo = curve[i - 1];
            const auto& hi = curve[i];
            return static_cast<uint8_t>(lo.percent +
                (voltage - lo.voltage) / (hi.voltage - lo.voltage) * (hi.percent - lo.percent) + 0.5f);
        }
    }
    return 100;
}

// Same trimmed mean / EMA / publication policy as the Arduino reference.
// ADC driver failures invalidate the whole batch; they cannot be trimmed away.
class Filter {
public:
    constexpr explicit Filter(Calibration calibration = {}) : calibration_(calibration) {}

    constexpr void sample(uint32_t mv, bool conversionOk = true) {
        samples_[count_++] = mv;
        batchError_ |= !conversionOk;
        if (count_ != kSampleCount) return;
        count_ = 0;
        candidate_.adcMillivolts = trimmedMean(samples_);
        const float voltage = packVoltage(candidate_.adcMillivolts, calibration_);
        candidate_.valid = !batchError_ && validMeasurement(candidate_.adcMillivolts, voltage);
        if (!candidate_.valid) {
            emaInitialized_ = false;
            ema_ = voltage;
        } else if (!emaInitialized_) {
            ema_ = voltage;
            emaInitialized_ = true;
        } else {
            ema_ += kEmaAlpha * (voltage - ema_);
        }
        candidate_.voltage = ema_;
        batchError_ = false;
        batchReady_ = true;
    }

    constexpr bool publish(int64_t nowUs) {
        if (!batchReady_ || (hasPublished_ && nowUs - lastPublishUs_ < kPublishIntervalUs)) return false;
        candidate_.valid &= validMeasurement(candidate_.adcMillivolts, candidate_.voltage);
        candidate_.percent = candidate_.valid ? voltageToPercent(candidate_.voltage) : 0;
        candidate_.low = candidate_.valid && lowState(published_.low, candidate_.voltage);
        published_ = candidate_;
        lastPublishUs_ = nowUs;
        hasPublished_ = true;
        batchReady_ = false;
        return true;
    }

    constexpr Reading reading() const { return published_; }

private:
    Calibration calibration_;
    Samples samples_{};
    unsigned count_ = 0;
    bool batchError_ = false;
    bool batchReady_ = false;
    bool emaInitialized_ = false;
    bool hasPublished_ = false;
    float ema_ = 0.0f;
    int64_t lastPublishUs_ = 0;
    Reading candidate_{};
    Reading published_{};
};
}  // namespace SpeakerBattery
