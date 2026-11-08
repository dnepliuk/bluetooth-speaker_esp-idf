#pragma once

namespace SpeakerControl {

// The sink init API only queues initialization. Hold upstream's startup request
// until the profile completion event; all access is on the A2DP event task.
class ReconnectStartup {
public:
    constexpr bool request() {
        if (ready_) return true;
        pending_ = true;
        return false;
    }

    constexpr bool onProfileState(bool initialized, bool reconnectAllowed) {
        ready_ = initialized;
        const bool start = ready_ && pending_ && reconnectAllowed;
        pending_ = false;  // Consume once, including a cancelled/failed startup.
        return start;
    }

private:
    bool ready_ = false;
    bool pending_ = false;
};

}  // namespace SpeakerControl
