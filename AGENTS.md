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
- Stage 1 implements startup diagnostics and GPIO2 heartbeat with Bluetooth/audio still pending. Flash is configured for 4 MiB with a 3 MiB factory application in `partitions.csv`; startup reports the physically detected flash size. Intentional values are in `sdkconfig.defaults`; the generated `sdkconfig.esp32doit-devkit-v1` remains tracked. Hardware acceptance is pending.

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

- Preserve separation of audio output, Bluetooth, media control, input, battery acquisition, OLED rendering and LED animation. Share state through explicit synchronization and keep critical sections short.
- Integrate existing components built on native I2S, PCNT, GPIO, calibrated ADC, I2C and RMT, following the reuse policy above. Check component support for the selected IDF version before adding dependencies. Retain existing application-level interfaces where practical.
- Replace Arduino `millis()`, `Serial`, GPIO/ADC wrappers and `loop()` with explicit ESP-IDF timing, logging and a scheduled FreeRTOS application loop. Avoid a busy-spin loop that starves idle tasks or causes watchdog resets.
- Build after coherent implementation changes. Add focused tests for nontrivial portable logic such as battery filtering, volume mapping, detent normalization and reconnect transitions; documentation-only changes need no firmware build.
- Validate on hardware only when authorized and after identifying the intended board and port. Do not flash, erase, or alter eFuses merely because a device is connected.
- Hardware acceptance covers audio at negotiated rates, pause/resume, volume at both endpoints, all buttons, reconnect after power cycle and link loss, manual switching between phones, and concurrent OLED/WS2812 updates without audio dropouts or reboots. Also check battery calibration/low/invalid states and operation with OLED absent.
- Report the exact environment, checks and limitations. A successful build is not proof of audio quality or hardware behavior. Keep Arduino-reference verification read-only.
