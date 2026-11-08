#include "bluetooth_audio.h"

#include <algorithm>
#include <atomic>
#include <cinttypes>
#include <cstring>
#include "BluetoothA2DPSink.h"
#include "audio_output.h"
#include "pcm_volume.h"
#include "pairing_policy.h"
#include "reconnect_startup.h"
#include "control_policy.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#if !CONFIG_BT_A2DP_ENABLE || !CONFIG_BT_CLASSIC_ENABLED
#error "Enable Classic Bluetooth and A2DP in sdkconfig.defaults"
#endif
#if CONFIG_BT_A2DP_USE_EXTERNAL_CODEC
#error "This adapter needs the ESP-IDF internal SBC decoder (PCM callback)"
#endif

namespace {
constexpr char kTag[] = "BLUETOOTH";
constexpr char kMediaTag[] = "MEDIA";
IdfI2sOutput output;
PcmVolume volume;
portMUX_TYPE stateMux = portMUX_INITIALIZER_UNLOCKED;
struct State {
    esp_a2d_connection_state_t connection = ESP_A2D_CONNECTION_STATE_DISCONNECTED;
    esp_a2d_audio_state_t audio = ESP_A2D_AUDIO_STATE_SUSPEND;
    uint32_t sampleRate = 0;
    uint8_t channels = 0;
    int volumeLevel = -1;
    bool volumeLocal = false;
    bool avrcConnected = false;
    SpeakerControl::Playback playback = SpeakerControl::Playback::Unknown;
    uint32_t session = 0;
    bool profileReady = false;
} state, reported;
std::atomic<uint32_t> ignoredPcmBytes{0};
using SpeakerControl::LinkState;
using SpeakerControl::PairingAction;
using SpeakerControl::PairingPhase;
class SpeakerSink;
SpeakerSink& sink();

LinkState linkState(esp_a2d_connection_state_t value) {
    switch (value) {
        case ESP_A2D_CONNECTION_STATE_CONNECTING: return LinkState::Connecting;
        case ESP_A2D_CONNECTION_STATE_CONNECTED: return LinkState::Connected;
        case ESP_A2D_CONNECTION_STATE_DISCONNECTING: return LinkState::Disconnecting;
        default: return LinkState::Disconnected;
    }
}

void onConnection(esp_a2d_connection_state_t connection, void*) {
    portENTER_CRITICAL(&stateMux);
    if (state.connection != connection) ++state.session;
    state.connection = connection;
    if (connection != ESP_A2D_CONNECTION_STATE_CONNECTED) {
        state.audio = ESP_A2D_AUDIO_STATE_SUSPEND;
        state.playback = SpeakerControl::Playback::Unknown;
    }
    if (connection == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
        state.sampleRate = 0;
        state.channels = 0;
        state.volumeLevel = -1;
    }
    portEXIT_CRITICAL(&stateMux);
}

void onAudio(esp_a2d_audio_state_t audio, void*) {
    portENTER_CRITICAL(&stateMux);
    state.audio = audio;
    portEXIT_CRITICAL(&stateMux);
}

void onVolume(int level) {
    portENTER_CRITICAL(&stateMux);
    state.volumeLevel = level;
    state.volumeLocal = false;
    portEXIT_CRITICAL(&stateMux);
}

State currentState() {
    portENTER_CRITICAL(&stateMux);
    const State snapshot = state;
    portEXIT_CRITICAL(&stateMux);
    return snapshot;
}

void onPlayback(esp_avrc_playback_stat_t playback) {
    using SpeakerControl::Playback;
    Playback confirmed = Playback::Unknown;
    switch (playback) {
        case ESP_AVRC_PLAYBACK_STOPPED: confirmed = Playback::Stopped; break;
        case ESP_AVRC_PLAYBACK_PLAYING: confirmed = Playback::Playing; break;
        case ESP_AVRC_PLAYBACK_PAUSED: confirmed = Playback::Paused; break;
        case ESP_AVRC_PLAYBACK_FWD_SEEK:
        case ESP_AVRC_PLAYBACK_REV_SEEK: confirmed = Playback::Seeking; break;
        default: break;
    }
    portENTER_CRITICAL(&stateMux);
    state.playback = confirmed;
    portEXIT_CRITICAL(&stateMux);
}

enum class Control : uint8_t { Volume, Toggle, Previous, Next };
struct ControlRequest {
    Control command;
    int delta;
    uint32_t session;
};

const char* mediaCommandName(int command) {
    switch (command) {
        case ESP_AVRC_PT_CMD_PLAY: return "Play";
        case ESP_AVRC_PT_CMD_PAUSE: return "Pause";
        case ESP_AVRC_PT_CMD_FORWARD: return "Next";
        case ESP_AVRC_PT_CMD_BACKWARD: return "Previous";
        default: return "Unknown";
    }
}

const char* mediaResponseName(esp_avrc_rsp_t response) {
    switch (response) {
        case ESP_AVRC_RSP_ACCEPT: return "accepted";
        case ESP_AVRC_RSP_REJECT: return "rejected";
        case ESP_AVRC_RSP_NOT_IMPL: return "not-implemented";
        case ESP_AVRC_RSP_IN_TRANS: return "in-transition";
        default: return "unexpected";
    }
}

// Keep protocol handling in the upstream library. These overrides adapt its
// output/error/lifetime policies to this application, without editing upstream.
class SpeakerSink final : public BluetoothA2DPSink {
public:
    explicit SpeakerSink(BluetoothA2DPOutput& target) : BluetoothA2DPSink(target) {
        s_volume_notify = false;
    }

    void requestPairing() { pairingRequested_.store(true); }

    esp_err_t initializeControls() {
        if (controlQueue_ != nullptr) return ESP_ERR_INVALID_STATE;
        controlQueue_ = xQueueCreate(12, sizeof(ControlRequest));
        return controlQueue_ ? ESP_OK : ESP_ERR_NO_MEM;
    }

    void requestControl(Control command, int delta = 0) {
        const State current = currentState();
        if (!controlQueue_ || current.connection != ESP_A2D_CONNECTION_STATE_CONNECTED ||
            pairingRequested_.load()) {
            if (command != Control::Volume) reportIgnored("Bluetooth disconnected or pairing");
            return;
        }
        if (command != Control::Volume && !current.avrcConnected) {
            reportIgnored("AVRCP not connected");
            return;
        }
        const ControlRequest request{command, delta, current.session};
        if (xQueueSend(controlQueue_, &request, 0) != pdTRUE) {
            reportIgnored("control queue full; input dropped");
        }
    }

    void dispatchControls() {
        if (!controlQueue_ || controlTickQueued_.load()) return;
        if (!keyPending_.load() && uxQueueMessagesWaiting(controlQueue_) == 0) return;
        controlTickQueued_.store(true);
        if (!postControlWork(onControlTick)) controlTickQueued_.store(false);
    }

    void dispatchPendingPairing() {
        if (!pairingRequested_.load() || pairingCommandQueued_.load()) return;
        const int64_t now = esp_timer_get_time();
        if (now < nextDispatchUs_) return;
        nextDispatchUs_ = now + 100000;
        pairingCommandQueued_.store(true);
        // No payload/allocation: library dispatch serializes this command with
        // connection events and frees only non-null payloads.
        if (!app_work_dispatch(onPairingCommand, 0, nullptr, 0)) {
            pairingCommandQueued_.store(false);
            if (!queueWarning_) ESP_LOGW(kTag, "Pairing command queue busy; will retry");
            queueWarning_ = true;
        } else {
            queueWarning_ = false;
        }
    }

    bool reconnect() override {
        if (!pairing_.allowsReconnect(pairingRequested_.load()) || !has_last_connection()) {
            return false;
        }
        if (!reconnectStartup_.request()) {
            ESP_LOGI(kTag, "Saved-peer reconnect deferred until A2DP is ready");
            return false;
        }
        ESP_LOGI(kTag, "Reconnect attempt %d/%d", connection_rety_count + 1,
                 try_reconnect_max_count);
        outgoingConnectionPending_ = BluetoothA2DPSink::reconnect();
        if (!outgoingConnectionPending_) {
            is_autoreconnect_allowed = false;
            reconnect_status = AutoReconnect;
            set_connectable(true);
            ESP_LOGW(kTag, "Reconnect request rejected; waiting for phone connection");
        }
        return outgoingConnectionPending_;
    }

protected:
    void execute_avrc_command(int command) override {
        // Public play/pause/next/previous still select upstream AVRCP commands.
        // Replace only its blocking delay_ms(100) with a later release event.
        const State current = currentState();
        const uint8_t label = nextLabel();
        const esp_err_t err = esp_avrc_ct_send_passthrough_cmd(
            label, command, ESP_AVRC_PT_CMD_STATE_PRESSED);
        if (err != ESP_OK) {
            ESP_LOGW(kMediaTag, "AVRCP TX %s press: key=0x%02x tl=%u error=%s",
                     mediaCommandName(command), command, label, esp_err_to_name(err));
            return;
        }
        heldKey_.press(command, esp_timer_get_time(), current.session);
        keyPending_.store(true);
        ESP_LOGI(kMediaTag, "AVRCP TX %s press: key=0x%02x tl=%u queued",
                 mediaCommandName(command), command, label);
    }

    void volume_set_by_local_host(uint8_t level) override {
        BluetoothA2DPSink::volume_set_by_local_host(level);
        // REGISTER_NOTIFICATION is one-shot: the next subscription's INTERIM
        // response carries the current level, including intervening rotation.
        s_volume_notify = false;
    }

    void av_hdl_avrc_evt(uint16_t event, void* parameter) override {
        BluetoothA2DPSink::av_hdl_avrc_evt(event, parameter);
        const auto* rc = static_cast<esp_avrc_ct_cb_param_t*>(parameter);
        // The SDK's send result only acknowledges its local queue. Log the
        // peer's response separately, without enabling BT_AV's PCM-path logs.
        // This runs on the existing library event task, not a GPIO/PCM callback.
        if (event == ESP_AVRC_CT_PASSTHROUGH_RSP_EVT) {
            const auto& response = rc->psth_rsp;
            ESP_LOG_LEVEL(response.rsp_code == ESP_AVRC_RSP_ACCEPT ? ESP_LOG_INFO : ESP_LOG_WARN,
                          kMediaTag, "AVRCP RX %s %s: key=0x%02x tl=%u rsp=%s (0x%02x)",
                          mediaCommandName(response.key_code),
                          response.key_state == ESP_AVRC_PT_CMD_STATE_PRESSED ? "press" : "release",
                          response.key_code, response.tl,
                          mediaResponseName(response.rsp_code), static_cast<unsigned>(response.rsp_code));
        } else if (event == ESP_AVRC_CT_REMOTE_FEATURES_EVT) {
            ESP_LOGI(kMediaTag, "AVRCP peer: features=0x%08" PRIx32 " target_flags=0x%04x",
                     rc->rmt_feats.feat_mask, rc->rmt_feats.tg_feat_flag);
        } else if (event == ESP_AVRC_CT_GET_RN_CAPABILITIES_RSP_EVT) {
            ESP_LOGI(kMediaTag, "AVRCP notifications: count=%u mask=0x%04x",
                     rc->get_rn_caps_rsp.cap_count, rc->get_rn_caps_rsp.evt_set.bits);
        }
        if (event == ESP_AVRC_CT_CONNECTION_STATE_EVT) {
            const bool connected = rc->conn_stat.connected;
            portENTER_CRITICAL(&stateMux);
            state.avrcConnected = connected;
            if (!connected) {
                ++state.session;
                state.playback = SpeakerControl::Playback::Unknown;
            }
            portEXIT_CRITICAL(&stateMux);
            if (!connected) clearHeldKey();
        }
    }

    void av_hdl_avrc_tg_evt(uint16_t event, void* parameter) override {
        BluetoothA2DPSink::av_hdl_avrc_tg_evt(event, parameter);
        if (event == ESP_AVRC_TG_CONNECTION_STATE_EVT &&
            !static_cast<esp_avrc_tg_cb_param_t*>(parameter)->conn_stat.connected) {
            s_volume_notify = false;
        }
    }

    bool is_reconnect(esp_a2d_disc_rsn_t reason) override {
        return pairing_.allowsReconnect(pairingRequested_.load()) &&
               BluetoothA2DPSink::is_reconnect(reason);
    }

    void init_nvs() override {
        // A full/incompatible NVS partition must be diagnosed, never erased here.
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    int init_bluetooth() override {
        const int ok = BluetoothA2DPSink::init_bluetooth();
        if (!ok) ESP_ERROR_CHECK(ESP_FAIL);
        return ok;
    }

    size_t write_audio(const uint8_t* data, size_t size) override {
        // Upstream i2s_write_data retries forever if an output returns zero.
        // The native driver reports partial/time-out writes; count/drop once.
        return output.write(data, size);
    }

    void handle_connection_state(uint16_t event, void* parameter) override {
        auto* p = static_cast<esp_a2d_cb_param_t*>(parameter);
        const auto next = p->conn_stat.state;
        const bool failedOutgoingAttempt = outgoingConnectionPending_ &&
            next == ESP_A2D_CONNECTION_STATE_DISCONNECTED;
        if (next == ESP_A2D_CONNECTION_STATE_DISCONNECTED ||
            next == ESP_A2D_CONNECTION_STATE_CONNECTED) {
            outgoingConnectionPending_ = false;
        }
        const bool cancellingLateConnection =
            pairing_.phase() == PairingPhase::WaitingForDisconnect &&
            next == ESP_A2D_CONNECTION_STATE_CONNECTED;
        if (cancellingLateConnection) {
            // Do not persist a connection that is still being cancelled.
            reconnect_status = NoReconnect;
        }
        if (!pairing_.allowsReconnect(pairingRequested_.load())) {
            is_autoreconnect_allowed = false;
        }
        if (next == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
            clearHeldKey();
            pcmChannels_.store(0);
            set_i2s_active(false);
            s_volume_notify = false;
        }
        auto normalized = *p;
        if (failedOutgoingAttempt && pairing_.allowsReconnect(pairingRequested_.load())) {
            // IDF 6.1 reports an unsuccessful OPEN with reason 0 (NORMAL).
            // Upstream interprets NORMAL as a user's disconnect and otherwise
            // stops after one failed attempt. Only an outstanding outgoing
            // attempt is a retryable failure; established links retain the
            // original reason, so an intentional phone disconnect is respected.
            normalized.conn_stat.disc_rsn = ESP_A2D_DISC_RSN_ABNORMAL;
        }
        BluetoothA2DPSink::handle_connection_state(event, &normalized);
        if (cancellingLateConnection) reconnect_status = AutoReconnect;

        const PairingAction action = pairing_.onConnection(linkState(next));
        if (next == ESP_A2D_CONNECTION_STATE_CONNECTED &&
            pairing_.phase() == PairingPhase::Normal) {
            // Upstream disables its runtime latch when no peer exists at boot
            // and on manual disconnect. Rearm AFTER a successful connection,
            // in this same event task, including the first-ever paired phone.
            reconnect_status = AutoReconnect;
            is_autoreconnect_allowed = true;
            ESP_LOGI(kTag, "Automatic reconnect armed (%d attempts)",
                     try_reconnect_max_count);
        }
        applyPairingAction(action);
        if (next == ESP_A2D_CONNECTION_STATE_DISCONNECTED &&
            pairing_.allowsReconnect(pairingRequested_.load()) &&
            !outgoingConnectionPending_) {
            if (connection_rety_count >= try_reconnect_max_count) {
                ESP_LOGI(kTag, "Reconnect limit reached; discoverable, waiting for phone");
            } else {
                ESP_LOGI(kTag, "Disconnected; discoverable, waiting for phone");
            }
        }
    }

    void handle_audio_cfg(uint16_t, void* parameter) override {
        pcmChannels_.store(0);
        auto* p = static_cast<esp_a2d_cb_param_t*>(parameter);
        const auto& codec = p->audio_cfg.mcc;
        uint32_t rate = 0;
        switch (codec.cie.sbc_info.samp_freq) {
            case ESP_A2D_SBC_CIE_SF_16K: rate = 16000; break;
            case ESP_A2D_SBC_CIE_SF_32K: rate = 32000; break;
            case ESP_A2D_SBC_CIE_SF_44K: rate = 44100; break;
            case ESP_A2D_SBC_CIE_SF_48K: rate = 48000; break;
            default: break;
        }
        if (codec.type != ESP_A2D_MCT_SBC || rate == 0) {
            ESP_LOGE(kTag, "Unsupported codec configuration; PCM remains muted");
            set_i2s_active(false);
            return;
        }
        const uint8_t channels =
            codec.cie.sbc_info.ch_mode == ESP_A2D_SBC_CIE_CH_MODE_MONO ? 1 : 2;
        output.set_sample_rate(rate);
        m_sample_rate = rate;
        m_channels = channels;
        audio_type = codec.type;
        pcmChannels_.store(channels);
        portENTER_CRITICAL(&stateMux);
        state.sampleRate = rate;
        state.channels = channels;
        portEXIT_CRITICAL(&stateMux);
    }

    void audio_data_callback(const uint8_t* data, uint32_t len) override {
        const uint8_t channels = pcmChannels_.load();
        if (channels == 0 || data == nullptr || len % (channels * 2) != 0) {
            ignoredPcmBytes.fetch_add(len);
            return;
        }
        if (channels == 2) {
            BluetoothA2DPSink::audio_data_callback(data, len);
            return;
        }
        // The native decoder can also negotiate mono. Duplicate each sample to
        // L/R before applying the unchanged stereo volume algorithm.
        Frame stereo[128];
        while (len != 0) {
            const uint32_t frames = std::min<uint32_t>(len / 2, 128);
            for (uint32_t i = 0; i < frames; ++i) {
                int16_t sample;
                std::memcpy(&sample, data + i * 2, sizeof(sample));
                stereo[i] = Frame(sample, sample);
            }
            BluetoothA2DPSink::audio_data_callback(
                reinterpret_cast<const uint8_t*>(stereo), frames * sizeof(Frame));
            data += frames * 2;
            len -= frames * 2;
        }
    }

    void av_hdl_a2d_evt(uint16_t event, void* parameter) override {
        BluetoothA2DPSink::av_hdl_a2d_evt(event, parameter);
        if (event == ESP_A2D_PROF_STATE_EVT) {
            auto* p = static_cast<esp_a2d_cb_param_t*>(parameter);
            const bool initialized = p->a2d_prof_stat.init_state == ESP_A2D_INIT_SUCCESS;
            const bool startReconnect = reconnectStartup_.onProfileState(
                initialized, pairing_.allowsReconnect(pairingRequested_.load()) &&
                    connection_state == ESP_A2D_CONNECTION_STATE_DISCONNECTED &&
                    !outgoingConnectionPending_);
            portENTER_CRITICAL(&stateMux);
            state.profileReady = initialized;
            portEXIT_CRITICAL(&stateMux);
            if (initialized) {
                // Emitted in the event task, before the first real connect call.
                ESP_LOGI(kTag, "A2DP ready; automatic reconnect enabled");
                if (startReconnect) reconnect();
            } else {
                ESP_LOGW(kTag, "A2DP profile not ready (state=%d)",
                         static_cast<int>(p->a2d_prof_stat.init_state));
            }
        }
    }

private:
    // Zero-payload work item in the EXISTING library queue. Unlike its default
    // sender this never waits for queue space, keeping the 10 ms loop responsive.
    bool postControlWork(app_callback_t callback) {
        if (!app_task_queue) return false;
        bt_app_msg_t message{};
        message.sig = BT_APP_SIG_WORK_DISPATCH;
        message.cb = callback;
        return xQueueSend(app_task_queue, &message, 0) == pdTRUE;
    }

    void reportIgnored(const char* reason) {
        // Main loop only; bound logging when disconnected or turning quickly.
        const int64_t now = esp_timer_get_time();
        if (now < nextIgnoredLogUs_) return;
        nextIgnoredLogUs_ = now + 1000000;
        ESP_LOGW(kMediaTag, "Input ignored: %s", reason);
    }

    uint8_t nextLabel() {
        // Upstream metadata/notification requests reserve labels 0..4.
        const uint8_t label = transactionLabel_;
        transactionLabel_ = transactionLabel_ == 15 ? 5 : transactionLabel_ + 1;
        return label;
    }

    void clearHeldKey() {
        heldKey_.clear();
        keyPending_.store(false);
        nextReleaseTryUs_ = 0;
        releaseWarning_ = false;
    }

    void releaseHeldKey(const State& current) {
        if (!heldKey_.active()) return;
        if (!heldKey_.belongsTo(current.session) || !current.avrcConnected ||
            current.connection != ESP_A2D_CONNECTION_STATE_CONNECTED) {
            clearHeldKey();  // Never send an old key release to a new peer.
            return;
        }
        const int command = heldKey_.command();
        const uint8_t label = nextLabel();
        const esp_err_t err = esp_avrc_ct_send_passthrough_cmd(
            label, command, ESP_AVRC_PT_CMD_STATE_RELEASED);
        if (err == ESP_OK) {
            ESP_LOGI(kMediaTag, "AVRCP TX %s release: key=0x%02x tl=%u queued",
                     mediaCommandName(command), command, label);
            clearHeldKey();
        } else {
            if (!releaseWarning_) {
                ESP_LOGW(kMediaTag, "AVRCP TX %s release: key=0x%02x tl=%u error=%s; will retry",
                         mediaCommandName(command), command, label, esp_err_to_name(err));
            }
            releaseWarning_ = true;
            nextReleaseTryUs_ = esp_timer_get_time() + 100000;
        }
    }

    static void onControlTick(uint16_t, void*) {
        auto& self = sink();
        self.serviceControls();
        self.controlTickQueued_.store(false);
    }

    void serviceControls() {
        const State current = currentState();
        const int64_t now = esp_timer_get_time();
        if (heldKey_.active()) {
            if (!heldKey_.belongsTo(current.session) ||
                (heldKey_.due(now) && now >= nextReleaseTryUs_)) {
                releaseHeldKey(current);
            }
            if (heldKey_.active()) return;
        }
        // Bounded drain; each media press waits for its release before the
        // next command. Local volume uses the latest s_volume on this task,
        // serialized with remote volume events rather than stale main-loop reads.
        ControlRequest request{};
        for (unsigned i = 0; i < 12 && xQueueReceive(controlQueue_, &request, 0) == pdTRUE; ++i) {
            if (!SpeakerControl::acceptsControl(request.session, current.session,
                    current.connection == ESP_A2D_CONNECTION_STATE_CONNECTED,
                    !pairing_.allowsReconnect(pairingRequested_.load()))) continue;
            if (request.command == Control::Volume) {
                const uint8_t level = SpeakerControl::adjustedVolume(get_volume(), request.delta);
                if (level != get_volume()) {
                    set_volume(level);
                    portENTER_CRITICAL(&stateMux);
                    state.volumeLevel = level;
                    state.volumeLocal = true;
                    portEXIT_CRITICAL(&stateMux);
                }
            } else if (current.avrcConnected) {
                switch (request.command) {
                    case Control::Toggle:
                        if (SpeakerControl::shouldPause(current.playback,
                            current.audio == ESP_A2D_AUDIO_STATE_STARTED)) pause();
                        else play();
                        break;
                    case Control::Previous: previous(); break;
                    case Control::Next: next(); break;
                    default: break;
                }
                return;
            }
        }
    }

    static void onPairingCommand(uint16_t, void*) {
        auto& self = sink();
        self.beginPairing();
        self.pairingRequested_.store(false);
        self.pairingCommandQueued_.store(false);
    }

    void beginPairing() {
        if (pairing_.phase() != PairingPhase::Normal) return;
        releaseHeldKey(currentState());
        clearHeldKey();  // Pairing will disconnect the peer even if release failed.
        portENTER_CRITICAL(&stateMux);
        ++state.session;  // Cancel queued controls as soon as pairing starts.
        portEXIT_CRITICAL(&stateMux);
        // Keep AutoReconnect selected so the next accepted peer is stored by
        // upstream. Its separate runtime latch suppresses outgoing retries.
        is_autoreconnect_allowed = false;
        is_target_status_active = false;
        reconnect_status = AutoReconnect;
        LinkState current = linkState(connection_state);
        if (current == LinkState::Disconnected && outgoingConnectionPending_) {
            current = LinkState::Connecting;  // CONNECTING callback not delivered yet.
        }
        ESP_LOGI(kTag, "Manual pairing requested; automatic reconnect suspended");
        const PairingAction action = pairing_.request(current);
        if (pairing_.phase() == PairingPhase::WaitingForDisconnect) {
            ESP_LOGI(kTag, "Pairing: waiting for disconnect");
        }
        applyPairingAction(action);
    }

    void applyPairingAction(PairingAction action) {
        switch (action) {
            case PairingAction::Disconnect: {
                // The current peer may differ from the persisted peer during
                // an incoming connection. Cancel the actual active attempt.
                const esp_err_t err = esp_a2d_sink_disconnect(peer_bd_addr);
                if (err != ESP_OK) {
                    pairing_.cancel();
                    is_autoreconnect_allowed =
                        connection_state == ESP_A2D_CONNECTION_STATE_CONNECTED;
                    ESP_LOGE(kTag, "Pairing disconnect rejected: %s; press button to retry",
                             esp_err_to_name(err));
                }
                break;
            }
            case PairingAction::MakeDiscoverable:
                set_discoverability(ESP_BT_GENERAL_DISCOVERABLE);
                set_connectable(true);
                ESP_LOGI(kTag, "Pairing: discoverable, waiting for phone; bonds preserved");
                break;
            case PairingAction::Complete:
                ESP_LOGI(kTag, "Manual pairing completed; automatic reconnect restored");
                break;
            case PairingAction::None: break;
        }
    }

    SpeakerControl::PairingPolicy pairing_;
    SpeakerControl::ReconnectStartup reconnectStartup_;
    QueueHandle_t controlQueue_ = nullptr;
    SpeakerControl::HeldMediaKey heldKey_;   // Bluetooth event task only.
    std::atomic<bool> keyPending_{false};
    std::atomic<bool> controlTickQueued_{false};
    uint8_t transactionLabel_ = 5;
    int64_t nextReleaseTryUs_ = 0;
    bool releaseWarning_ = false;
    int64_t nextIgnoredLogUs_ = 0;           // Main loop only.
    std::atomic<bool> pairingRequested_{false};
    std::atomic<bool> pairingCommandQueued_{false};
    bool outgoingConnectionPending_ = false;  // A2DP event task only.
    int64_t nextDispatchUs_ = 0;              // Main loop only.
    bool queueWarning_ = false;              // Main loop only.
    std::atomic<uint8_t> pcmChannels_{0};
};

SpeakerSink& sink() {
    static SpeakerSink instance(output);
    return instance;
}
}  // namespace

namespace BluetoothAudio {
esp_err_t begin(const Config& config) {
    if (config.deviceName == nullptr || config.initialVolume > 127 || config.reconnectAttempts == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t nvs = nvs_flash_init();
    if (nvs != ESP_OK) {
        ESP_LOGE(kTag, "NVS init failed: %s; existing data has been preserved",
                 esp_err_to_name(nvs));
        return nvs;
    }
    const esp_err_t i2s = output.initialize(config);
    if (i2s != ESP_OK) return i2s;

    // Keep upstream informational logging out of the PCM callback path.
    esp_log_level_set("BT_AV", ESP_LOG_WARN);
    esp_log_level_set("RCCT", ESP_LOG_WARN);
    esp_log_level_set("BT_API", ESP_LOG_WARN);
    auto& a2dp = sink();
    const esp_err_t controls = a2dp.initializeControls();
    if (controls != ESP_OK) return controls;
    a2dp.set_volume_control(&volume);
    a2dp.set_volume(config.initialVolume);
    a2dp.set_on_volumechange(onVolume);
    a2dp.set_on_connection_state_changed(onConnection);
    a2dp.set_on_audio_state_changed(onAudio);
    a2dp.set_avrc_rn_playstatus_callback(onPlayback);
    a2dp.set_default_bt_mode(ESP_BT_MODE_CLASSIC_BT);
    a2dp.set_auto_reconnect(true, config.reconnectAttempts);
    a2dp.set_event_stack_size(4096);
    a2dp.set_avrc_metadata_attribute_mask(0);
    a2dp.start(config.deviceName);
    ESP_LOGI(kTag, "Starting '%s'; phone volume=0..127, fallback=%u; reconnect=%u attempts",
             config.deviceName, static_cast<unsigned>(config.initialVolume),
             static_cast<unsigned>(config.reconnectAttempts));
    return ESP_OK;
}

void update() {
    portENTER_CRITICAL(&stateMux);
    const State current = state;
    portEXIT_CRITICAL(&stateMux);
    if (current.profileReady) sink().dispatchPendingPairing();
    if (current.profileReady) sink().dispatchControls();
    if (current.connection != reported.connection) {
        ESP_LOGI(kTag, "Connection: %s",
                 sink().to_str(current.connection));
    }
    if (current.sampleRate != 0 &&
        (current.sampleRate != reported.sampleRate || current.channels != reported.channels)) {
        ESP_LOGI(kTag, "SBC negotiated: %" PRIu32 " Hz, %u channel(s); I2S s16 stereo",
                 current.sampleRate, static_cast<unsigned>(current.channels));
    }
    if (current.audio != reported.audio) {
        ESP_LOGI(kTag, "Audio: %s",
                 current.audio == ESP_A2D_AUDIO_STATE_STARTED ? "started" : "suspended");
    }
    if (current.avrcConnected != reported.avrcConnected) {
        ESP_LOGI(kMediaTag, "AVRCP: %s", current.avrcConnected ? "connected" : "disconnected");
    }
    if (current.volumeLevel >= 0 && (current.volumeLevel != reported.volumeLevel ||
                                    current.volumeLocal != reported.volumeLocal)) {
        ESP_LOGI(kMediaTag, "%s volume: %d/127",
                 current.volumeLocal ? "Encoder" : "Phone", current.volumeLevel);
    }
    if (current.playback != reported.playback) {
        using SpeakerControl::Playback;
        ESP_LOGI(kMediaTag, "Playback: %s",
                 current.playback == Playback::Playing ? "playing" :
                 current.playback == Playback::Paused ? "paused" :
                 current.playback == Playback::Stopped ? "stopped" :
                 current.playback == Playback::Seeking ? "seeking" : "unknown (A2DP fallback)");
    }
    reported = current;
}

void enterPairingMode() {
    sink().requestPairing();
}

void requestMedia(MediaCommand command) {
    switch (command) {
        case MediaCommand::TogglePlayPause: sink().requestControl(Control::Toggle); break;
        case MediaCommand::Previous: sink().requestControl(Control::Previous); break;
        case MediaCommand::Next: sink().requestControl(Control::Next); break;
    }
}

void adjustVolume(int delta) {
    if (delta != 0) sink().requestControl(Control::Volume, delta);
}

void logStats() {
    output.logStats();
    const uint32_t ignored = ignoredPcmBytes.load();
    if (ignored != 0) ESP_LOGW(kTag, "PCM ignored before valid format: %" PRIu32 " bytes", ignored);
}
}  // namespace BluetoothAudio
