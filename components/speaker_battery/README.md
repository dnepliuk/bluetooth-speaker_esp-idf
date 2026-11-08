# Battery measurement and OLED

Product integration of ESP-IDF 6.1 `esp_adc`, `esp_driver_i2c`, and
[chill-sam/ssd1306 1.1.2](https://components.espressif.com/components/chill-sam/ssd1306/versions/1.1.2/readme)
(MIT, ESP-IDF >=5.3, ESP32 supported). Its driver, framebuffer and
drawing API replace Arduino's Adafruit_SSD1306/GFX. It uses the current native
I2C master API and needs no Arduino, LVGL, display timer or graphics stack.
The component manifest pins upstream Git commit
`d95eaac62a8fc697b0a63cc001ae3dfca48aee66` (tag `v1.1.2`). Component Manager owns
downloads and `dependencies.lock`. The [upstream source](https://github.com/Chill-Sam/esp-ssd1306/tree/d95eaac62a8fc697b0a63cc001ae3dfca48aee66)
and downloaded dependency remain unchanged. The Registry archive for this release
contains nested `dist` copies and exceeds Windows path limits during extraction;
the Git dependency avoids that packaging issue without changing library code.

The Registry's `espressif/ssd1306` is deprecated. The built-in `esp_lcd` SSD1306
driver is suitable for panel transport but still needs text/drawing support.
The chosen component supplies both at the scale required for this one screen;
`nixy4/u8g2` was also considered but adds a broader display/font configuration.

## ADC and filtering

`BatteryMonitor::begin` maps GPIO34 to ADC1 channel 6, uses 12-bit oneshot reads
and line-fitting calibration (two-point or Vref eFuse, otherwise the same 1100 mV
default as Arduino-ESP32 2.0.17). Native `ADC_ATTEN_DB_12` has value 3, equivalent
to the reference's legacy `ADC_11db`. Calibration provenance is logged. No eFuses
or NVS are written. A failed ADC setup leaves the reading invalid and audio runs.

The main loop takes at most one calibrated reading per 10 ms; missed service
slots never trigger a burst of catch-up conversions. Twenty readings form one
batch. Sorting twenty integers implements the reference's trimmed mean, discarding
two readings at either end, then rounding the mean of the remaining sixteen.
Convert with the divider/gain/offset from `src/app_config.h`, then apply EMA 0.20.
The first batch publishes immediately; subsequent latest results publish at
one-second intervals. No ADC task, interrupt handler, dynamic filter storage or
PCM-path work is added.

The 3S curve is unchanged: 10.2/10.8/11.1/11.4/11.7/12.0/12.3/12.6 V map to
0/10/20/35/55/75/90/100%, interpolated and rounded. LOW enters at <=10.8 V and
clears at >=11.1 V on the filtered result. Pack values outside 8.0..13.2 V,
zero/out-of-range ADC mV and failed conversions yield invalid readings. Invalid
batches reset EMA; the next valid batch seeds it directly. Driver failures
invalidate the whole batch even if their placeholder sample would be trimmed.
Invalid readings never display a stale percentage. Routine logs are limited to
once per five seconds; low transitions are logged separately.

This is a voltage-based indicator, not a coulomb counter. Load sag and recovery
after Pause are expected. Physical calibration, an open input that happens to
float to a plausible voltage, and actual battery capacity cannot be verified in
software. Use the battery/divider measurements described in the hardware tests.

## Display ownership and failure handling

`BatteryDisplay::begin` creates one priority-1 FreeRTOS task with a 4096-byte stack
and a one-element snapshot queue. The worker exclusively owns I2C0, the display
handle and its 1024-byte framebuffer. The main loop overwrites the latest snapshot
without waiting; Bluetooth/PCM callbacks never touch display state. OLED discovery
and initialization run after Bluetooth startup, on that worker, with no blocking
wait in the main loop. At most one queued snapshot exists; rendering never queues
stale frames.

The native bus probes 0x3C with a 50 ms deadline, SDA21/SCL22, internal pull-ups;
the library uses 400 kHz synchronous I2C. It hardcodes `-1` (infinite wait) in
`i2c_master_transmit`. This component's CMake applies a **private compile definition
only to that pinned library target**, redirecting those calls to
`speaker_oled_i2c_transmit`, which caps each transfer to 20 ms. The adapter calls
the unmodified SDK API. No other I2C consumer is redirected. Do not remove this
adapter without revisiting the library's timeout behavior.

An absent OLED, failed initialization or transfer error disables display work for
the rest of that boot, logs once and releases display/bus/task resources. A small
one-slot queue stays allocated so a concurrent main-loop publisher cannot touch
freed memory. There is no automatic hot-plug recovery; reconnect the OLED with
power off and restart. Audio and battery sampling keep running.

The unchanged `fonts/adafruit/glcdfont.c` and accompanying BSD `license.txt` are
copied read-only from the reference's installed Adafruit GFX 1.12.6. The native
driver's public font API renders this 5-column, 8-row asset; no Arduino/GFX code
or framework is linked. This preserves the reference's glyph shapes as well as
spacing (the driver's default font has different digits/letters).

The Arduino layout is preserved: percent at (0,2), scale 3; battery outline
(91,7,32,18), terminal (123,12,3,8), fill up to 28x14; voltage at (0,31), scale 2;
LOW BATTERY at (29,55), scale 1. A tiny text-layout wrapper preserves Adafruit's
6*scale character advance while the library renders glyphs. Invalid data shows
BATTERY / ERROR / --.- V at the reference positions. Normal refresh is one second,
state/percentage changes trigger refresh, and the low icon alternates every
500 ms. A 50 ms worker wait checks blink deadlines without spinning.

The pure filter tests compile with firmware and generate no runtime test code.
See [hardware validation](../../docs/testing.md) for acceptance and calibration.
