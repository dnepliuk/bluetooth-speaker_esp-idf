#include "control_policy.h"
#include <climits>

using namespace SpeakerControl;
static_assert(adjustedVolume(64, 4) == 68 && adjustedVolume(64, -4) == 60);
static_assert(adjustedVolume(126, 4) == 127 && adjustedVolume(127, -4) == 123);
static_assert(adjustedVolume(1, -4) == 0 && adjustedVolume(0, 4) == 4);
static_assert(adjustedVolume(64, INT_MAX) == 127 && adjustedVolume(64, INT_MIN) == 0);
static_assert(acceptsControl(7, 7, true, false));
static_assert(!acceptsControl(7, 8, true, false) && !acceptsControl(7, 7, false, false));
static_assert(!acceptsControl(7, 7, true, true));
static_assert(shouldPause(Playback::Playing, false) && shouldPause(Playback::Seeking, false));
static_assert(!shouldPause(Playback::Paused, true) && !shouldPause(Playback::Stopped, true));
static_assert(shouldPause(Playback::Unknown, true) && !shouldPause(Playback::Unknown, false));
constexpr bool holdAndRelease() {
    HeldMediaKey key;
    if (key.active()) return false;
    key.press(0x44, 1000, 9);
    if (!key.active() || key.due(100999) || !key.due(101000)) return false;
    if (key.command() != 0x44 || !key.belongsTo(9) || key.belongsTo(10)) return false;
    key.clear();
    return !key.active() && !key.due(200000);
}
static_assert(holdAndRelease());
