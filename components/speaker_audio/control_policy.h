#pragma once

#include <cstdint>

namespace SpeakerControl {
constexpr uint8_t adjustedVolume(int current, int delta) {
    const int64_t value = static_cast<int64_t>(current) + delta;
    return value < 0 ? 0 : value > 127 ? 127 : static_cast<uint8_t>(value);
}

constexpr bool acceptsControl(uint32_t requestSession, uint32_t activeSession,
                              bool connected, bool pairing) {
    return connected && !pairing && requestSession == activeSession;
}

// Protocol values are mapped by the adapter; status is confirmed by the peer.
enum class Playback { Unknown, Stopped, Playing, Paused, Seeking };
constexpr bool shouldPause(Playback confirmed, bool audioStarted) {
    return confirmed == Playback::Unknown ? audioStarted :
           confirmed == Playback::Playing || confirmed == Playback::Seeking;
}

class HeldMediaKey {
public:
    constexpr void press(int command, int64_t now, uint32_t session) {
        command_ = command;
        session_ = session;
        releaseAt_ = now + 100000;
    }
    constexpr bool active() const { return command_ >= 0; }
    constexpr bool due(int64_t now) const { return active() && now >= releaseAt_; }
    constexpr bool belongsTo(uint32_t session) const { return active() && session_ == session; }
    constexpr int command() const { return command_; }
    constexpr void clear() { command_ = -1; }
private:
    int command_ = -1;
    uint32_t session_ = 0;
    int64_t releaseAt_ = 0;
};
}  // namespace SpeakerControl
