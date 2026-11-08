#pragma once

#include <cstdint>

#include "driver/gpio.h"
#include "battery_types.h"

#ifndef ENCODER_REVERSED
#define ENCODER_REVERSED false
#endif

namespace AppConfig {

constexpr char kBluetoothName[] = "Faital Bluetooth Speaker";
constexpr uint32_t kSerialBaud = 115200;
constexpr uint32_t kExpectedFlashBytes = 4U * 1024U * 1024U;
constexpr uint32_t kApplicationPartitionBytes = 3U * 1024U * 1024U;
constexpr uint32_t kInitialSampleRate = 44100;
constexpr uint8_t kVolumeFallback = 64;
constexpr int kVolumeStep = 4;
constexpr int kEncoderCountsPerDetent = 2;
constexpr bool kEncoderReversed = ENCODER_REVERSED;
// Original 1023 APB cycles at 80 MHz = 12787.5 ns. Round up; the IDF
// driver converts 12788 ns back to exactly 1023 hardware filter cycles.
constexpr uint32_t kEncoderGlitchNs = 12788;
constexpr uint8_t kReconnectAttempts = 5;
constexpr unsigned kButtonDebounceMs = 40;
constexpr unsigned kButtonLongPressMs = 800;

// Fixed wiring from the working reference.
constexpr gpio_num_t kI2sBckPin = GPIO_NUM_26;
constexpr gpio_num_t kI2sWsPin = GPIO_NUM_25;
constexpr gpio_num_t kI2sDataPin = GPIO_NUM_19;
constexpr gpio_num_t kEncoderAPin = GPIO_NUM_32;
constexpr gpio_num_t kEncoderBPin = GPIO_NUM_33;
constexpr gpio_num_t kEncoderSwitchPin = GPIO_NUM_18;
constexpr gpio_num_t kPreviousButtonPin = GPIO_NUM_13;
constexpr gpio_num_t kNextButtonPin = GPIO_NUM_14;
constexpr gpio_num_t kBluetoothButtonPin = GPIO_NUM_23;
constexpr gpio_num_t kBatteryAdcPin = GPIO_NUM_34;
constexpr gpio_num_t kOledSdaPin = GPIO_NUM_21;
constexpr gpio_num_t kOledSclPin = GPIO_NUM_22;
constexpr uint8_t kOledAddress = 0x3C;
constexpr SpeakerBattery::Calibration kBatteryCalibration{
    150000, 30000, 0.9880f, 0.0f};

// GPIO27 stays LOW until the WS2812 stage.
constexpr gpio_num_t kStatusLedPin = GPIO_NUM_2;
constexpr bool kStatusLedActiveHigh = true;
constexpr gpio_num_t kLedStripPin = GPIO_NUM_27;
constexpr uint32_t kStatusLedHeartbeatPeriodMs = 1000;
constexpr uint32_t kStatusLedHeartbeatOnMs = 50;
constexpr uint32_t kServiceIntervalMs = 10;
constexpr int64_t kRuntimeLogIntervalUs = 10LL * 1000LL * 1000LL;

static_assert(kStatusLedHeartbeatOnMs < kStatusLedHeartbeatPeriodMs);
static_assert(kStatusLedHeartbeatOnMs % kServiceIntervalMs == 0);

}  // namespace AppConfig
