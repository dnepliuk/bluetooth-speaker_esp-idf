#pragma once

#include <cstdint>

namespace SpeakerBattery {
struct Calibration {
    uint32_t highOhm = 150000;
    uint32_t lowOhm = 30000;
    float gain = 0.9880f;
    float offsetV = 0.0f;
};

struct Reading {
    float voltage = 0.0f;
    uint32_t adcMillivolts = 0;
    uint8_t percent = 0;
    bool valid = false;
    bool low = false;
};

constexpr int64_t kSampleIntervalUs = 10000;
constexpr int64_t kPublishIntervalUs = 1000000;
constexpr int64_t kLogIntervalUs = 5000000;
constexpr int64_t kDisplayIntervalUs = 1000000;
constexpr int64_t kBlinkIntervalUs = 500000;
constexpr unsigned kSampleCount = 20;
constexpr unsigned kTrimPerEnd = 2;
constexpr float kEmaAlpha = 0.20f;
constexpr float kValidMinV = 8.0f;
constexpr float kValidMaxV = 13.2f;
constexpr float kLowEnterV = 10.8f;
constexpr float kLowClearV = 11.1f;
constexpr uint32_t kAdcMaxMillivolts = 3300;
}  // namespace SpeakerBattery
