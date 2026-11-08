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
struct Status {
    bool ready;
    bool connected;
    bool playing;
    bool pairing;
};
// Read-only indication snapshot; confirmed AVRCP playback, A2DP fallback.
Status status();
// Nonblocking request; cancellation and pairing run on the library's event task.
void enterPairingMode();
enum class MediaCommand : uint8_t { TogglePlayPause, Previous, Next };
// Called by the main loop. Requests are bounded, nonblocking and scoped to
// the current connection; disconnected input is discarded, never replayed.
void requestMedia(MediaCommand command);
void adjustVolume(int delta);
void logStats();

}  // namespace BluetoothAudio
