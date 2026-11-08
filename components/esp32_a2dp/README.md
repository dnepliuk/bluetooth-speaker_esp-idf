# ESP32-A2DP native ESP-IDF component

This is the same Bluetooth library/version as the working Arduino reference:

- Upstream: https://github.com/pschatzmann/ESP32-A2DP
- Tag: `v1.8.11`
- Commit: `b6da4744286ca15b6f1dee607c077a4747294dd4`
- Vendored: the complete upstream `src/` directory and `LICENSE`, unchanged.
- Source archive SHA-256: `a8b73ecd3415d01c97540d029c90e8e996a136f44d1fd4dc93341f0335dbdfcf`
- License: Apache-2.0; see [upstream/LICENSE](upstream/LICENSE).

The project-owned CMake wrapper builds the common, sink and output translation
units, with `A2DP_LEGACY_I2S_SUPPORT=0`, `A2DP_I2S_AUDIOTOOLS=0`,
and `A2DP_SPP_SUPPORT=0`. It explicitly includes sdkconfig before upstream headers.
No Arduino component, AudioTools, Source or SinkQueued implementation is linked.
The upstream CMakeLists (which requires AudioTools) is deliberately not used.
The wrapper also maps the single upstream `delay(2100)` call to the library's
native `delay_ms`, declares `esp_ringbuf` for its header dependency, and treats
upstream includes as system headers. Missing-field initializer warnings are
suppressed only while compiling the library: IDF 6 adds fields that are
zero-initialized by its existing aggregate initializers. Project code keeps
the normal compiler warnings.

This matches upstream's documented native/custom-output mode:
https://github.com/pschatzmann/ESP32-A2DP/wiki/Espressif-IDF-as-a-Component

Compatibility is pinned to this project's ESP32 / ESP-IDF 6.1.0 build.
The internal SBC decoder / legacy PCM callback is intentional:
`CONFIG_BT_A2DP_USE_EXTERNAL_CODEC=n`. Encoded SBC must never be sent to I2S.
A future SDK migration must revisit that callback, which Espressif plans to remove.

The Registry alternative `espressif/esp_bt_audio` was evaluated. It exposes
encoded packets and requires an external decoder/pipeline. Reusing ESP32-A2DP
keeps the reference's AVRCP behavior and nonlinear volume control with fewer
new layers. Native `esp_driver_i2s` supplies the actual I2S driver.

Product-specific adaptations belong in [../speaker_audio](../speaker_audio):

- Configure the native I2S standard driver for PCM5102A.
- Preserve NVS on initialization errors (override upstream erase-and-retry).
- Use one bounded driver write instead of upstream's retry-until-complete loop.
- Decode the IDF 6.1 SBC format fields; duplicate mono PCM to stereo if negotiated.
- Publish volume atomically and evaluate upstream's unchanged gain/sample scaling
  on the PCM task, avoiding shared mutable gain state between AVRCP and audio.
- Serialize manual pairing with the existing control-event queue, suppress
  outgoing retries during pairing and rearm them after a successful connection.
- Defer the startup reconnect request until the A2DP profile reports successful
  initialization; the init API's return only confirms that initialization was queued.
- Treat an IDF 6.1 failed outgoing OPEN as retryable even when its reported
  disconnect reason is NORMAL; preserve real intentional disconnection reasons.

No new PCM queue, writer task, codec, resampler or Bluetooth stack is implemented.
The library's existing control task and ESP-IDF's BTC thread remain in use.
