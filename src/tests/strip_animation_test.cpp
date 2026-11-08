#include "strip_animation.h"
#include <limits>

using namespace StripAnimation;
constexpr uint8_t brightness = 48;
constexpr Frame init = frameFor(State::Initializing, 0, brightness);
constexpr Frame waiting = frameFor(State::Waiting, 0, brightness);
static_assert(stateFor(false, true, true, true) == State::Initializing);
static_assert(stateFor(true, false, true, false) == State::Waiting);
static_assert(stateFor(true, true, true, true) == State::Waiting);
static_assert(stateFor(true, true, false, false) == State::ConnectedIdle);
static_assert(stateFor(true, true, true, false) == State::Playing);
static_assert(init[0] == Pixel{48, 15, 0} && init[1] == Pixel{} && init[7] == Pixel{});
static_assert(frameFor(State::Initializing, 99, brightness) == init);
static_assert(frameFor(State::Initializing, 100, brightness)[1] == init[0]);
static_assert(frameFor(State::Initializing, 700, brightness)[7] == init[0] &&
              frameFor(State::Initializing, 800, brightness) == init);
static_assert(waiting[0] == Pixel{0, 9, 48} && waiting[7] == Pixel{0, 2, 12} && waiting[1] == Pixel{});
static_assert(frameFor(State::Waiting, 149, brightness) == waiting);
static_assert(frameFor(State::Waiting, 150, brightness)[1] == waiting[0] &&
              frameFor(State::Waiting, 150, brightness)[0] == waiting[7]);
static_assert(frameFor(State::Waiting, 1200, brightness) == waiting);
static_assert(frameFor(State::ConnectedIdle, 0, brightness)[0] == Pixel{});
static_assert(frameFor(State::ConnectedIdle, 300, brightness)[0] == Pixel{0, 9, 11});
static_assert(frameFor(State::ConnectedIdle, 600, brightness)[0] == Pixel{0, 18, 21});
static_assert(frameFor(State::ConnectedIdle, std::numeric_limits<uint64_t>::max(), brightness) ==
              frameFor(State::ConnectedIdle, 600, brightness));
static_assert(frameFor(State::Playing, 0, brightness)[0] == Pixel{0, 1, 1});
static_assert(frameFor(State::Playing, 1200, brightness)[0] == Pixel{0, 42, 30});
static_assert(frameFor(State::Playing, 600, brightness) == frameFor(State::Playing, 1800, brightness) &&
              frameFor(State::Playing, 0, brightness) == frameFor(State::Playing, 2400, brightness));

constexpr bool animationLifecycle() {
    Animator animator(1000, brightness);
    Frame frame{};
    if (!animator.update(1000, frame) || frame != init) return false;
    if (animator.update(1099, frame) || animator.setState(State::Initializing, 1099)) return false;
    if (!animator.update(1100, frame) || frame[1] != init[0]) return false;
    // A state transition is immediate even when the old interval is not due.
    if (!animator.setState(State::Waiting, 1101) || !animator.update(1101, frame) || frame != waiting) return false;
    if (animator.update(1250, frame) || !animator.update(1251, frame)) return false;
    if (frame[1] != waiting[0] || frame[0] != waiting[7]) return false;
    animator.setState(State::ConnectedIdle, 1300);
    if (!animator.update(1300, frame) || frame != Frame{}) return false;
    if (!animator.update(1900, frame) || frame[0] != Pixel{0, 18, 21}) return false;
    if (animator.update(1940, frame) || animator.setState(State::ConnectedIdle, 2000)) return false;
    if (animator.update(2500, frame)) return false;
    animator.setState(State::Playing, 2501);
    if (!animator.update(2501, frame) || frame[0] != Pixel{0, 1, 1}) return false;
    if (!animator.update(3701, frame) || frame[0] != Pixel{0, 42, 30}) return false;
    // Returning to idle restarts its fade; a delayed update uses elapsed time.
    animator.setState(State::ConnectedIdle, 3800);
    if (!animator.update(4100, frame) || frame[0] != Pixel{0, 9, 11}) return false;
    animator.setState(State::Waiting, 4200);
    return animator.update(4950, frame) && frame[5] == waiting[0] && frame[4] == waiting[7];
}
static_assert(animationLifecycle());

constexpr bool breathingBoundsAndSymmetry() {
    Pixel previous{};
    for (uint32_t ms = 0; ms <= 1200; ms += 40) {
        const auto rising = frameFor(State::Playing, ms, brightness);
        if (rising != frameFor(State::Playing, 2400 - ms, brightness)) return false;
        for (const auto& pixel : rising) {
            if (pixel != rising[0] || pixel.red != 0 || pixel.green > 42 || pixel.blue > 30) return false;
        }
        if (rising[0].green < previous.green || rising[0].blue < previous.blue) return false;
        previous = rising[0];
    }
    return true;
}
static_assert(breathingBoundsAndSymmetry());
