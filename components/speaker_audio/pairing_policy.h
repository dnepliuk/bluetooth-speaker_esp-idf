#pragma once

namespace SpeakerControl {

enum class LinkState { Disconnected, Connecting, Connected, Disconnecting };
enum class PairingPhase { Normal, WaitingForDisconnect, Discoverable };
enum class PairingAction { None, Disconnect, MakeDiscoverable, Complete };

// Owned by the A2DP event task. GPIO/main-loop requests are queued to that task.
// The upstream library continues to own connection attempts and peer storage.
class PairingPolicy {
public:
    constexpr PairingPhase phase() const { return phase_; }
    constexpr void cancel() { phase_ = PairingPhase::Normal; }

    constexpr bool allowsReconnect(bool requestPending) const {
        return !requestPending && phase_ == PairingPhase::Normal;
    }

    constexpr PairingAction request(LinkState link) {
        if (phase_ != PairingPhase::Normal) return PairingAction::None;
        if (link == LinkState::Disconnected) {
            phase_ = PairingPhase::Discoverable;
            return PairingAction::MakeDiscoverable;
        }
        phase_ = PairingPhase::WaitingForDisconnect;
        return link == LinkState::Disconnecting ? PairingAction::None
                                               : PairingAction::Disconnect;
    }

    constexpr PairingAction onConnection(LinkState link) {
        if (phase_ == PairingPhase::WaitingForDisconnect) {
            if (link == LinkState::Disconnected) {
                phase_ = PairingPhase::Discoverable;
                return PairingAction::MakeDiscoverable;
            }
            // An outgoing attempt can finish while its cancellation is pending.
            // Finish disconnecting it before accepting the next incoming peer.
            if (link == LinkState::Connected) return PairingAction::Disconnect;
        } else if (phase_ == PairingPhase::Discoverable && link == LinkState::Connected) {
            phase_ = PairingPhase::Normal;
            return PairingAction::Complete;
        }
        return PairingAction::None;
    }

private:
    PairingPhase phase_ = PairingPhase::Normal;
};

}  // namespace SpeakerControl
