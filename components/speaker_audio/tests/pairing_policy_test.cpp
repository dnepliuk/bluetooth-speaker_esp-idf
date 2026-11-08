#include "pairing_policy.h"
#include "reconnect_startup.h"

using namespace SpeakerControl;

// Compile-time scenario tests run on every firmware build; no test task or
// host compiler is required and no test code occupies the firmware image.
constexpr bool pairFromIdle() {
    PairingPolicy policy;
    return policy.request(LinkState::Disconnected) == PairingAction::MakeDiscoverable &&
           !policy.allowsReconnect(false) &&
           policy.request(LinkState::Disconnected) == PairingAction::None &&
           policy.onConnection(LinkState::Connecting) == PairingAction::None &&
           policy.onConnection(LinkState::Connected) == PairingAction::Complete &&
           policy.allowsReconnect(false);
}

constexpr bool cancelAnOutgoingAttempt() {
    PairingPolicy policy;
    if (policy.allowsReconnect(true)) return false;  // Request not dispatched yet.
    if (policy.request(LinkState::Connecting) != PairingAction::Disconnect) return false;
    if (policy.allowsReconnect(false)) return false;
    if (policy.request(LinkState::Connecting) != PairingAction::None) return false;
    if (policy.onConnection(LinkState::Connected) != PairingAction::Disconnect) return false;
    if (policy.phase() != PairingPhase::WaitingForDisconnect) return false;
    if (policy.onConnection(LinkState::Disconnected) != PairingAction::MakeDiscoverable) return false;
    // A different phone (or the previous phone) can now initiate a connection.
    return policy.onConnection(LinkState::Connected) == PairingAction::Complete &&
           policy.allowsReconnect(false);
}

constexpr bool switchConnectedPhone() {
    PairingPolicy policy;
    if (policy.request(LinkState::Connected) != PairingAction::Disconnect) return false;
    if (policy.onConnection(LinkState::Disconnecting) != PairingAction::None) return false;
    if (policy.onConnection(LinkState::Disconnected) != PairingAction::MakeDiscoverable) return false;
    // Failure of an incoming connection must leave pairing enabled.
    if (policy.onConnection(LinkState::Connecting) != PairingAction::None) return false;
    if (policy.onConnection(LinkState::Disconnected) != PairingAction::None) return false;
    if (policy.allowsReconnect(false)) return false;
    return policy.onConnection(LinkState::Connected) == PairingAction::Complete &&
           policy.request(LinkState::Connected) == PairingAction::Disconnect;
}

constexpr bool waitForExistingDisconnect() {
    PairingPolicy policy;
    return policy.request(LinkState::Disconnecting) == PairingAction::None &&
           policy.phase() == PairingPhase::WaitingForDisconnect &&
           !policy.allowsReconnect(false) &&
           policy.onConnection(LinkState::Disconnected) == PairingAction::MakeDiscoverable;
}

constexpr bool normalConnectionEventsDoNotStartPairing() {
    PairingPolicy policy;
    return policy.onConnection(LinkState::Connecting) == PairingAction::None &&
           policy.onConnection(LinkState::Connected) == PairingAction::None &&
           policy.onConnection(LinkState::Disconnected) == PairingAction::None &&
           policy.phase() == PairingPhase::Normal && policy.allowsReconnect(false);
}

constexpr bool rejectedDisconnectCanBeRetried() {
    PairingPolicy policy;
    policy.request(LinkState::Connected);
    policy.cancel();
    return policy.phase() == PairingPhase::Normal &&
           policy.request(LinkState::Connected) == PairingAction::Disconnect;
}

static_assert(pairFromIdle(), "Idle pairing must accept the next peer and rearm reconnect");
static_assert(cancelAnOutgoingAttempt(), "Pairing must cancel retries and late connections");
static_assert(switchConnectedPhone(), "Pairing must survive failed incoming connections");
static_assert(waitForExistingDisconnect(), "Do not duplicate an active disconnect");
static_assert(normalConnectionEventsDoNotStartPairing(), "Normal events must retain normal mode");
static_assert(rejectedDisconnectCanBeRetried(), "An API error must not leave pairing stuck");

constexpr bool startupWaitsForProfileCompletion() {
    ReconnectStartup startup;
    if (startup.request()) return false;  // Never call the SDK while init is queued.
    if (startup.request()) return false;  // Repeated requests must also be deferred.
    if (!startup.onProfileState(true, true)) return false;
    if (!startup.request()) return false; // The deferred request can now reach the SDK.
    return !startup.onProfileState(true, true);  // Completion must not dial twice.
}

constexpr bool startupWithoutSavedPeerStaysIdle() {
    ReconnectStartup startup;
    if (startup.onProfileState(true, true)) return false;  // Nothing was requested.
    return startup.request();  // Later library retries do not need a new init event.
}

constexpr bool pairingCancelsDeferredStartup() {
    ReconnectStartup startup;
    PairingPolicy pairing;
    if (startup.request()) return false;
    // GPIO request was published before the profile became ready.
    if (startup.onProfileState(true, pairing.allowsReconnect(true))) return false;
    pairing.request(LinkState::Disconnected);
    if (startup.onProfileState(true, pairing.allowsReconnect(false))) return false;
    pairing.onConnection(LinkState::Connected);
    // A later ready event must not resurrect the cancelled startup attempt.
    return !startup.onProfileState(true, pairing.allowsReconnect(false));
}

constexpr bool unavailableProfileCannotStartReconnect() {
    ReconnectStartup startup;
    if (startup.request()) return false;
    if (startup.onProfileState(false, true)) return false;
    if (startup.onProfileState(true, true)) return false;
    if (!startup.request()) return false;
    // Also block SDK calls if a ready profile later becomes unavailable.
    if (startup.onProfileState(false, true)) return false;
    return !startup.request();
}

constexpr bool incomingConnectionConsumesDeferredStartup() {
    ReconnectStartup startup;
    if (startup.request()) return false;
    // An incoming link already exists; the caller disallows an outgoing attempt.
    if (startup.onProfileState(true, false)) return false;
    return !startup.onProfileState(true, true);
}

static_assert(startupWaitsForProfileCompletion(), "Do not connect before A2DP INIT_SUCCESS");
static_assert(startupWithoutSavedPeerStaysIdle(), "Profile ready alone must not start reconnect");
static_assert(pairingCancelsDeferredStartup(), "Manual pairing takes priority over startup reconnect");
static_assert(unavailableProfileCannotStartReconnect(), "Failed/deinitialized profiles cannot connect");
static_assert(incomingConnectionConsumesDeferredStartup(), "Do not redial after an incoming connection");
