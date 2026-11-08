#pragma once

#include <cstdint>
#include "esp_err.h"

namespace BluetoothAudio {

struct Config {
    const char* deviceName;
    int bckPin;
    int wsPin;
    int dataPin;
    uint32_t initialSampleRate;
    uint8_t initialVolume;
    uint8_t reconnectAttempts;
};

// Called once from app_main; uses the library's own Bluetooth tasks.
esp_err_t begin(const Config& config);
// Event logs are coalesced and emitted from the application service loop.
void update();
// Nonblocking request; cancellation and pairing run on the library's event task.
void enterPairingMode();
void logStats();

}  // namespace BluetoothAudio
