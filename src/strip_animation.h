#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace StripAnimation {
enum class State : uint8_t { Initializing, Waiting, ConnectedIdle, Playing };
constexpr size_t kPixelCount = 8;
struct Pixel {
    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 0;
    constexpr bool operator==(const Pixel&) const = default;
};
using Frame = std::array<Pixel, kPixelCount>;

constexpr State stateFor(bool ready, bool connected, bool playing, bool pairing) {
    if (!ready) return State::Initializing;
    if (pairing || !connected) return State::Waiting;
    return playing ? State::Playing : State::ConnectedIdle;
}

constexpr uint32_t intervalMs(State state) {
    return state == State::Initializing ? 100 : state == State::Waiting ? 150 : 40;
}

// Adafruit NeoPixel stores brightness+1, then shifts by eight. Preserve its
// two separate integer truncations (animation scale, then global brightness).
constexpr Pixel scaled(Pixel color, uint8_t scale, uint8_t brightness) {
    const auto channel = [=](uint8_t value) constexpr -> uint8_t {
        return ((value * scale / 255) * (static_cast<unsigned>(brightness) + 1)) >> 8;
    };
    return {channel(color.red), channel(color.green), channel(color.blue)};
}

constexpr Frame frameFor(State state, uint64_t elapsedMs, uint8_t brightness) {
    Frame frame{};
    switch (state) {
        case State::Initializing:
            frame[(elapsedMs / 100) % kPixelCount] = scaled({255, 80, 0}, 255, brightness);
            break;
        case State::Waiting: {
            const size_t head = (elapsedMs / 150) % kPixelCount;
            frame[head] = scaled({0, 48, 255}, 255, brightness);
            frame[(head + kPixelCount - 1) % kPixelCount] = scaled({0, 12, 64}, 255, brightness);
            break;
        }
        case State::ConnectedIdle: {
            const uint8_t scale = elapsedMs >= 600 ? 255 : 12 + 243 * elapsedMs / 600;
            frame.fill(scaled({0, 96, 112}, scale, brightness));
            break;
        }
        case State::Playing: {
            const uint32_t phase = elapsedMs % 2400;
            const uint32_t ramp = phase < 1200 ? phase : 2400 - phase;
            const uint8_t triangle = ramp * 255 / 1200;
            const uint8_t scale = 12 + 243 * triangle / 255;
            frame.fill(scaled({0, 220, 160}, scale, brightness));
            break;
        }
    }
    return frame;
}

class Animator {
public:
    constexpr Animator(uint64_t nowMs, uint8_t brightness)
        : stateStartMs_(nowMs), lastFrameMs_(nowMs), brightness_(brightness) {}

    constexpr State state() const { return state_; }
    constexpr bool setState(State next, uint64_t nowMs) {
        if (next == state_) return false;
        state_ = next;
        stateStartMs_ = nowMs;
        force_ = true;
        return true;
    }

    // Skip unchanged pixels, including the steady idle frame after its fade.
    // Time is absolute: delayed updates skip ahead, never build a frame queue.
    constexpr bool update(uint64_t nowMs, Frame& frame) {
        if (!force_ && nowMs - lastFrameMs_ < intervalMs(state_)) return false;
        force_ = false;
        lastFrameMs_ = nowMs;
        const Frame next = frameFor(state_, nowMs - stateStartMs_, brightness_);
        if (hasFrame_ && next == displayed_) return false;
        displayed_ = next;
        hasFrame_ = true;
        frame = next;
        return true;
    }

private:
    State state_ = State::Initializing;
    uint64_t stateStartMs_;
    uint64_t lastFrameMs_;
    uint8_t brightness_;
    bool force_ = true;
    bool hasFrame_ = false;
    Frame displayed_{};
};
}  // namespace StripAnimation
