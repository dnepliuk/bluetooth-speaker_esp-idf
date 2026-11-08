# Bluetooth Speaker: ESP-IDF Project Instructions

## Scope and protected reference

- This project is `C:/projects/bluetooth-speaker_esp-idf`. Its goal is a native ESP-IDF implementation with the observable behavior of the working Arduino speaker.
- `C:/projects/a2dp-arduino-minimal` is a READ-ONLY reference. Never edit, format, clean, build, install dependencies into, or otherwise write to that directory, including its `.pio` and `.git` directories. Inspect its source and installed libraries only by reading.
- Implement changes in this ESP-IDF project. Preserve unrelated existing work. Keep changes within the user's current request; preparing instructions or analyzing the project does not authorize implementing or flashing firmware.
- Use native ESP-IDF APIs without Arduino as a component. C++ is acceptable and useful for preserving the existing module boundaries; use an `extern "C"` entry point for `app_main` when implemented in C++.

## Staged delivery

- Implement only the current agreed migration stage. Build and provide a short hardware test procedure, then wait for the user's test result.
- Fix failures within the current stage. Move to the next stage only after the user confirms hardware acceptance and their commit. The user makes commits; do not commit automatically.
- Stage order: (1) startup/configuration/heartbeat; (2) A2DP, I2S and phone volume; (3) reconnect and pairing button; (4) encoder and media buttons; (5) battery/OLED; (6) WS2812 and full regression.
- Keep `README.md` focused on the project: purpose, hardware, architecture, dependencies, configuration and general build/use instructions. Put per-stage test procedures, expected logs and acceptance results in `docs/testing.md` or the chat.
- Follow `docs/testing.md` for the current stage and acceptance checks. Compile success is separate from hardware PASS.

## Skills and toolchain

- Use [esp-idf](C:/Users/1dima/.codex/skills/esp-idf/SKILL.md) for ESP-IDF configuration, implementation, build failures and runtime diagnostics.
- Use [esp-idf-components](C:/Users/1dima/.codex/skills/esp-idf-components/SKILL.md) when adding components, selecting dependencies, or changing manifests and component CMake files.
- This repository uses PlatformIO, environment `esp32doit-devkit-v1`, board `esp32doit-devkit-v1`, target `esp32`, framework `espidf`. Prefer this established workflow over introducing a second SDK installation or replacing PlatformIO.
- Stage 1 pins PlatformIO platform `espressif32@7.1.3` and `platformio/framework-espidf@4.60100.0` (ESP-IDF 6.1.0). Preserve these versions unless a later task explicitly requires a justified change.
- The Arduino reference uses Arduino-ESP32 2.0.17 / ESP-IDF 4.4.7, ESP32-A2DP v1.8.11 and AudioTools v1.2.5. Read its `dependencies.lock` and installed implementation when reproducing library behavior.
- Consult official Espressif documentation and local headers/examples matching the selected IDF version and ESP32 target. Do not mix IDF 4.x, 5.x and 6.x APIs.
- Stages 1–5 were committed by the user; stage 5 is `ad876c1`. Its 0.5.0 test confirmed normal battery/OLED startup and voltage agreement with a multimeter, with zero ADC/PCM errors in a 3m38s log. LOW/invalid states, missing OLED and endurance were not confirmed. Stage 4 (`a59b94b`), 0.4.1 with iPhone/YouTube Music, confirmed all media buttons and encoder behavior, 14 commands with 28 accepted press/release responses and zero PCM drops/write errors in a short log. The prior 0.4.0 failure's cause remains unknown; 0.4.1 only added diagnostics. Stage 3 (`a8cf519`) confirmed reconnect after Reset and battery power-on, phone/PC operation, five-attempt limit and pairing-button cancellation followed by manual connection. Remaining regression gaps and SDK/UART diagnostics are in `docs/testing.md`; do not report them as passed. Stage 2's track-boundary clicks and shortened endurance test remain open. The user authorized stage 6: WS2812 and full regression; hardware acceptance is pending. Flash remains 4 MiB with a 3 MiB factory app; intentional configuration is in `sdkconfig.defaults`, generated sdkconfig remains tracked.
- Stage 6 uses unmodified `espressif/led_strip` 3.1.1 with a sole priority-1 worker on core 1, a one-slot latest-state queue, and 256 RMT symbols for the complete eight-pixel frame (no refill interrupt). `src/status_strip.cpp` supplies the reference 800 kHz timing through the public custom-timing API; `strip_animation.h` preserves animation colors, periods and NeoPixel brightness rounding. Do not call strip refresh from main/BT/audio callbacks. Main-loop status publication never waits for hardware. `BluetoothAudio::status()` exposes synchronized state; confirmed AVRCP playback takes precedence over A2DP and pairing overrides connected/playing indication. Keep upstream and managed sources unchanged. See `docs/ws2812.md` for ownership and failure behavior.
- Stage 5 uses `components/speaker_battery`: native ADC1 oneshot/line-fitting calibration plus chill-sam/ssd1306 v1.1.2 at Git commit `d95eaac62a8fc697b0a63cc001ae3dfca48aee66`, managed through Component Manager. The Registry zip has nested dist paths that fail on Windows; keep the Git pin. Keep upstream sources unchanged. Its I2C timeout is capped to 20 ms through a private target compile definition and a thin transport adapter. The separate priority-1 OLED worker owns its framebuffer/I2C and receives a single latest snapshot; audio and main-loop input must never wait for OLED. ADC sampling stays in the main loop. Missing OLED disables display for the current boot; ADC failures produce invalid readings. Preserve the battery filter tests and reference layout. See the component README for ownership, calibration and error behavior.
- Stage 4 reuses `esp_driver_pcnt`, the existing `espressif/button` and ESP32-A2DP. Keep the reference half-quadrature edge/level actions, two counts per detent, +/-4 volume step and 40 ms short-release button policy. Keep local/remote volume serialized on the library event task, controls scoped to the connection generation, and the 100 ms media-key release nonblocking. Use confirmed AVRCP playback with A2DP fallback. Input callbacks must not perform Bluetooth calls or log; see the speaker component README for the bounded control queue and one-shot volume notification handling.
- Stage 3 reuses ESP32-A2DP's retry/NVS mechanisms and `espressif/button` 4.2.3. Preserve the Component Manager manifest and generated `dependencies.lock`; do not edit `managed_components/`. Pairing commands and library reconnect flags are owned by the Bluetooth event task, with atomic requests from the main loop. Keep the constexpr pairing/startup scenario checks in the component build. Startup reconnect must wait for ESP_A2D_PROF_STATE_EVT / ESP_A2D_INIT_SUCCESS; version 0.3.0 failed hardware testing by calling connect before initialization completed (ESP_ERR_INVALID_STATE 259). See the component README for this readiness gate, the IDF 6.1 failed-OPEN reason adaptation and the runtime-latch rearm.
- Keep `components/esp32_a2dp/upstream` unchanged; its sources are pinned to the reference's upstream commit. Build adaptation lives in its parent CMakeLists; product adaptation lives in `components/speaker_audio`. See the component README for provenance and the scoped overrides for NVS, PCM format handling and bounded writes.

Build from PowerShell after implementation changes are authorized:

```powershell
& 'C:\Users\1dima\.platformio\penv\Scripts\pio.exe' run -d 'C:\projects\bluetooth-speaker_esp-idf' -e esp32doit-devkit-v1
```

If that executable is unavailable, locate the active PlatformIO environment. Do not silently build with another ESP-IDF version. Use explicit component source lists and `REQUIRES` / `PRIV_REQUIRES`. Store intentional configuration in `sdkconfig.defaults` and applicable target defaults; inspect the effective regenerated configuration because existing sdkconfig values can override defaults. Do not hand-edit generated build artifacts or installed/managed dependencies.

## Library and component reuse (user requirement)

- Build the firmware by integrating existing libraries and ESP-IDF components, as in the Arduino project. Keep custom code focused on thin adapters, configuration and speaker-specific behavior.
- Before implementing a subsystem, inspect built-in ESP-IDF facilities, the ESP Component Registry and maintained upstream libraries. Prefer suitable existing components after checking the exact ESP-IDF version, ESP32 support, behavior, memory use and maintenance.
- Prefer ready-made OLED drivers/renderers, LED-strip drivers and input-handling components where they satisfy the hardware and behavior contract. Reuse Espressif's Bluetooth stack, codecs and peripheral drivers; do not recreate these layers.
- Component reuse must preserve the required audio quality, volume synchronization, reconnect behavior, encoder counting and indication. Avoid a large framework when a smaller component meets the same requirements.
- Record selected dependencies, version constraints and the reason for each choice. Use native ESP-IDF-compatible components; do not introduce the Arduino framework to reuse a library without an explicit change to the agreed architecture.
- If no suitable component covers a requirement, explain the specific gap and limit new code to that gap. Do not default to rewriting a whole driver or subsystem.

## Behavior and hardware contract

Use the reference's current source code as the behavioral baseline. Its README contains stale sections: the actual battery divider is 150/30 kOhm with gain 0.9880, and GPIO2 is a heartbeat, not a Bluetooth status pattern.

| Function | Required baseline |
| --- | --- |
| Bluetooth identity | `Faital Bluetooth Speaker`; Bluetooth Classic A2DP Sink with Bluedroid |
| Audio output | PCM5102A, I2S TX master, Philips format, signed 16-bit stereo; initial 44100 Hz; follow the negotiated sample rate |
| I2S pins | BCK GPIO26, WS GPIO25, DATA GPIO19; no RX; preserve GPIO19 |
| Encoder | EC11 A GPIO32, B GPIO33, internal pull-ups, half-quadrature PCNT; two raw counts per detent; clockwise increases volume by default |
| Buttons | Encoder switch GPIO18 = Play/Pause; GPIO13 = Previous; GPIO14 = Next; GPIO23 = manual Bluetooth pairing; active-low with internal pull-ups |
| Volume | 0..127, step 4, application fallback 64; phone/encoder synchronization through AVRCP Absolute Volume |
| Battery | ADC1 GPIO34, 12-bit calibrated millivolts; attenuation equivalent to the original ADC_11db setting |
| OLED | SSD1306 128x64, address 0x3C; SDA GPIO21, SCL GPIO22; battery indication only |
| RGB strip | Eight WS2812, GPIO27, GRB 800 kHz, global brightness 48/255 |
| Status LED | GPIO2 active-high; 50 ms pulse every 1000 ms, driven by the application service loop |
| Logs | UART 115200; bounded event/state logging |

### Audio and Bluetooth details

- Preserve PCM channel order, negotiated rate handling and the original volume curve. The reference library applies `A2DPDefaultVolumeControl` to PCM using a nonlinear curve; implementing AVRCP messages alone does not reproduce audible volume.
- Reproduce AVRCP Controller media commands and Target absolute-volume handling, including notification subscriptions and local/remote state updates. Distinguish connection state from confirmed playback state.
- Preserve last-peer persistence, a five-attempt reconnect policy, and discoverability after failed reconnect. Reference persistence uses NVS namespace `connected_bda`, key `last_bda`; inspect compatibility before reusing existing data.
- Manual pairing must disable competing outgoing reconnect attempts, request disconnect asynchronously, become connectable/discoverable, remember the next connected peer, and restore normal reconnect behavior. Preserve bonding; do not erase NVS as a routine initialization or pairing step.
- A previously paired phone can initiate an incoming connection during manual pairing. The reference does not implement an address blacklist.
- The reference application has no additional PCM queue, writer task, resampler, EQ or Wi-Fi. Keep the initial port minimal. If callback timing requires buffering, justify and bound its size, ownership and overflow behavior.
- In IDF 6.1, the legacy PCM callback and the external-codec encoded-audio callback have different contracts. Verify the selected API and codec configuration; never pass encoded SBC data directly to I2S.
- Reconfigure I2S sample rate safely. Keep Bluetooth callbacks short; defer logging, display access and peripheral work. Do not retain callback buffer pointers beyond their documented lifetime.

### Input, battery and indication details

- Buttons use 40 ms debounce and one single-click action, with no hold repeats. Check OneButton's actual click timing before replacing it; debounce duration alone does not specify the complete click behavior.
- Preserve encoder direction and detent normalization. The reference PCNT filter is 1023 legacy clock cycles; convert to the selected driver's units rather than copying that number into a nanosecond field.
- Battery conversion: `Vbat = Vadc * ((150000 + 30000) / 30000) * 0.9880 + 0.0`, with Vadc in volts. Preserve this initial baseline, then validate calibration on hardware after changing ADC APIs.
- Sample every 10 ms: collect 20 readings, discard two lowest and two highest, average, then apply EMA alpha 0.20. Publish approximately every 1000 ms and rate-limit routine battery logs to 5000 ms.
- Preserve the voltage-to-percentage curve in `battery_monitor.cpp`. LOW BATTERY enters at <=10.8 V and clears at >=11.1 V. Preserve invalid-reading behavior, the 8.0..13.2 V pack range and protection against invalid batches contaminating the EMA.
- OLED shows percentage, voltage, battery icon, LOW BATTERY and BATTERY ERROR. Preserve the existing layout, one-second normal refresh and 500 ms low-battery icon blink. Missing OLED must not prevent Bluetooth audio from starting.
- Preserve strip colors and animation timing from `app_config.h` / `led_strip.cpp`: initializing moving amber point, waiting blue point with tail, connected-idle cyan fade, playing teal breathing. Waiting also covers pairing and reconnect. Do not add PCM visualization.
- Drive GPIO27 LOW early during startup and clear the strip. Missing optional indication must not stop audio.
- Keep GPIO2 heartbeat dependent on application-loop progress; an independent timer heartbeat would change its diagnostic meaning.

## Implementation and validation

- The first 0.6.0 hardware test confirmed all four WS2812 states, pairing and playback, with zero PCM/ADC errors in a 68-second log. Full regression/endurance remains pending. Version 0.6.1 releases app_main's temporary GPIO27 reservation with the pinned SDK's `esp_gpio_revoke()` immediately before starting the strip worker. Preserve active LOW during this handoff; `gpio_reset_pin()` disables output and enables pull-up. Release no unrelated pins and do not suppress RMT warnings. Recheck this private SDK API when upgrading ESP-IDF.

- Preserve separation of audio output, Bluetooth, media control, input, battery acquisition, OLED rendering and LED animation. Share state through explicit synchronization and keep critical sections short.
- Integrate existing components built on native I2S, PCNT, GPIO, calibrated ADC, I2C and RMT, following the reuse policy above. Check component support for the selected IDF version before adding dependencies. Retain existing application-level interfaces where practical.
- Replace Arduino `millis()`, `Serial`, GPIO/ADC wrappers and `loop()` with explicit ESP-IDF timing, logging and a scheduled FreeRTOS application loop. Avoid a busy-spin loop that starves idle tasks or causes watchdog resets.
- Build after coherent implementation changes. Add focused tests for nontrivial portable logic such as battery filtering, volume mapping, detent normalization and reconnect transitions; documentation-only changes need no firmware build.
- Validate on hardware only when authorized and after identifying the intended board and port. Do not flash, erase, or alter eFuses merely because a device is connected.
- Hardware acceptance covers audio at negotiated rates, pause/resume, volume at both endpoints, all buttons, reconnect after power cycle and link loss, manual switching between phones, and concurrent OLED/WS2812 updates without audio dropouts or reboots. Also check battery calibration/low/invalid states and operation with OLED absent.
- Report the exact environment, checks and limitations. A successful build is not proof of audio quality or hardware behavior. Keep Arduino-reference verification read-only.
