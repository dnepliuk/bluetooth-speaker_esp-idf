# Speaker audio integration

Public entry points are in `include/bluetooth_audio.h`. The application supplies
the fixed wiring, device name, initial rate and initial volume.

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

NVS is initialized without erase-on-error. Automatic reconnect is disabled for
this stage; the existing library still accepts incoming phone connections.
See [../../docs/testing.md](../../docs/testing.md) for hardware checks.
