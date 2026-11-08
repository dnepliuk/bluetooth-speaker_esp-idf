#pragma once

#include <cstdint>

// Convert accumulated PCNT counts into complete detents without read/clear
// races or losing a half-detent when the direction changes. Main-loop only.
class RotarySteps {
public:
    constexpr explicit RotarySteps(int countsPerDetent, bool reversed = false)
        : countsPerDetent_(countsPerDetent), reversed_(reversed) {}

    constexpr int64_t take(int32_t count) {
        const uint32_t raw = static_cast<uint32_t>(count);
        const uint32_t distance = raw - previous_;
        previous_ = raw;
        const int64_t delta = distance <= INT32_MAX ? static_cast<int64_t>(distance)
                                                  : static_cast<int64_t>(distance) - (1LL << 32);
        const int64_t total = remainder_ + delta;
        const int64_t detents = total / countsPerDetent_;
        remainder_ = total % countsPerDetent_;
        return reversed_ ? -detents : detents;
    }

private:
    int countsPerDetent_;
    bool reversed_;
    uint32_t previous_ = 0;
    int64_t remainder_ = 0;
};
