# Speaker audio integration

Public entry points are in `include/bluetooth_audio.h`. The application supplies
the fixed wiring, device name, initial rate, initial volume and reconnect limit.

`bluetooth_audio.cpp` configures ESP32-A2DP. Protocol callbacks update protected
state; the application loop emits coalesced status logs. Phone volume is applied
using the upstream `A2DPDefaultVolumeControl` curve through `pcm_volume.h`.
Volume 0 is mute; volume 127 retains upstream's 4095/4096 maximum gain.
The initial advertised/applied volume is 64 until the phone updates it.

`audio_output.cpp` implements the library's output interface using
`esp_driver_i2s`: Philips, signed 16-bit stereo, APLL, no MCLK output.
A mutex serializes rate changes and DMA writes. Six 240-frame DMA descriptors
hold 5760 bytes (about 33 ms at 44.1 kHz). Auto-clear supplies silence when no PCM
arrives; clocks remain active during pause. Rate changes disable the channel,
reconfigure its clock, preload zeroes and enable it again.

PCM pointers are consumed synchronously before returning to ESP-IDF. There is
no application PCM queue and no extra writer task. In IDF 6.1 the internal
SBC decoder/PCM callback runs on the existing BTC thread. The direct DMA write
keeps the reference's simple audio path; verify uninterrupted sound and prompt
Pause/Play/volume response on hardware. Its BTC stack is configured to 4096 bytes.
Driver waits use a 100 ms timeout; partial/error writes are counted and their
remaining bytes are dropped without retrying forever. Control callbacks run in
ESP32-A2DP's existing event task. The main loop never writes PCM.
Errors are visible through periodic counters.

The internal decoder accepts SBC rates 16/32/44.1/48 kHz. Hardware acceptance
must state which rates were actually tested. No EQ or resampling is applied.

NVS is initialized without erase-on-error. ESP32-A2DP owns last-peer persistence
(`connected_bda` / `last_bda`, a six-byte blob) and automatic reconnect with five
attempts. That format matches the Arduino reference. Neither manual pairing nor
failed attempts erase bonds. A normal disconnect initiated by the phone leaves
the speaker waiting for an incoming connection; abnormal link loss starts retrying.

`enterPairingMode()` publishes a request; `update()` dispatches it without a
payload to the library's existing control queue once A2DP is ready. A busy queue
is retried every 100 ms, with one warning per busy interval. No additional
Bluetooth task or reconnect loop is introduced. State changes, retry flags and
NVS persistence all run on the same event task as the upstream handlers.
Pairing/retry logs are emitted there; they do not run in the PCM callback.

`pairing_policy.h` handles the product's manual-pairing transitions. An active
or outgoing connection must finish disconnecting before a new phone is accepted.
A late CONNECTED event during cancellation is disconnected again and is not
persisted. IDF may finish or time out an in-progress OPEN before honoring the
disconnect; the main loop and heartbeat keep running during that wait. Repeated
button clicks while pairing is active are ignored. The previous phone is not
blacklisted and may reconnect as an incoming peer once discoverable.

Three scoped compatibility adaptations keep the upstream reconnect mechanism usable:

- `esp_a2d_sink_init()` queues initialization. Upstream requests reconnect
  immediately afterwards, while IDF 6.1 can still reject it with
  ESP_ERR_INVALID_STATE (0x103/259). `reconnect_startup.h` defers that request
  until ESP_A2D_PROF_STATE_EVT / ESP_A2D_INIT_SUCCESS, then consumes it once
  on the same event task. No attempt is counted before readiness. A pending
  manual-pairing request or existing incoming connection cancels the deferred
  outgoing attempt. No timer, blocking wait or arbitrary startup delay is added.
- IDF 6.1's `btc_av_state_opening_handler` reports an unsuccessful OPEN with
  disconnect reason 0/NORMAL. Normalize only the terminal event of a pending
  outgoing attempt to ABNORMAL, so upstream reaches its configured retry limit.
  Established connections retain the actual reason, including manual disconnect.
- Rearm upstream's `is_autoreconnect_allowed` latch after every accepted connection,
  including the first phone on empty NVS and the phone selected by manual pairing.
  `set_auto_reconnect(true)` alone only sets the separate policy enum. Rearming in
  the event task avoids changing these fields concurrently from the main loop.

An outgoing request rejected by the SDK falls back to discoverable mode. An
immediately rejected pairing-disconnect request is logged and allows another
button press. These errors must be checked on hardware; they are not reported
as successful pairing.

`tests/pairing_policy_test.cpp` uses constexpr scenario tests/static assertions
for idle pairing, cancellation before CONNECTING, late connection completion,
failed incoming connections, repeated clicks, an already-running disconnect and
recovery after API rejection. Startup regression scenarios also cover deferred
requests, repeated completion events, no saved-peer request, unavailable profiles,
manual-pairing priority and an incoming connection taking precedence. They compile
with the firmware and add no runtime test code. Radio timing, persistence across
reset and audio still need hardware.

The native GPIO driver is `espressif/button` 4.2.3 (Apache-2.0), selected from
the Component Registry instead of porting Arduino's OneButton. A 10 ms scan and
four debounce samples preserve the 40 ms setting. `src/pairing_button.cpp`
handles debounced PRESS_UP, ignoring holds longer than 800 ms: OneButton 2.6.1
with only `attachClick` does not wait its default 400 ms multi-click window.
Native SINGLE_CLICK would add an unnecessary wait. The native timer callback
only publishes an atomic flag; the application loop requests pairing. GPIO23
has an internal pull-up; low-power wake/hold-repeat behavior is not enabled.

See [../../docs/testing.md](../../docs/testing.md) for hardware checks.
