#include "battery_filter.h"
#include <limits>

using namespace SpeakerBattery;
constexpr bool near(float a, float b) { return a > b - 0.0001f && a < b + 0.0001f; }
constexpr Samples constant(uint32_t mv) { Samples s{}; s.fill(mv); return s; }
constexpr void batch(Filter& filter, uint32_t mv, bool ok = true) {
    for (unsigned i = 0; i < kSampleCount; ++i) filter.sample(mv, ok);
}

static_assert(trimmedMean(constant(2000)) == 2000);
constexpr bool outliers() {
    auto s = constant(2000);
    s[0] = 0; s[1] = 20; s[18] = 3250; s[19] = 3300;
    return trimmedMean(s) == 2000;
}
static_assert(outliers());
constexpr bool roundingAndOverflow() {
    Samples s{};
    for (unsigned i = 0; i < s.size(); ++i) s[i] = 2000 + i;
    return trimmedMean(s) == 2010 && trimmedMean(constant(UINT32_MAX)) == UINT32_MAX;
}
static_assert(roundingAndOverflow());
static_assert(near(packVoltage(2000, {}), 11.856f));
static_assert(near(packVoltage(2000, {150000, 30000, 1.0f, 0.25f}), 12.25f));
static_assert(voltageToPercent(8.0f) == 0 && voltageToPercent(13.2f) == 100);
static_assert(voltageToPercent(10.2f) == 0 && voltageToPercent(10.8f) == 10);
static_assert(voltageToPercent(11.1f) == 20 && voltageToPercent(11.4f) == 35);
static_assert(voltageToPercent(11.7f) == 55 && voltageToPercent(12.0f) == 75);
static_assert(voltageToPercent(12.3f) == 90 && voltageToPercent(12.6f) == 100);
static_assert(voltageToPercent(11.55f) == 45);
static_assert(validMeasurement(2000, 8.0f) && validMeasurement(2000, 13.2f));
static_assert(!validMeasurement(0, 12.0f) && !validMeasurement(3301, 12.0f));
static_assert(!validMeasurement(2000, 7.99f) && !validMeasurement(2000, 13.21f));
static_assert(!validMeasurement(2000, std::numeric_limits<float>::quiet_NaN()) &&
              !validMeasurement(2000, std::numeric_limits<float>::infinity()));
static_assert(lowState(false, 10.8f) && !lowState(false, 10.81f));
static_assert(lowState(true, 11.09f) && !lowState(true, 11.1f));

constexpr bool publicationCadence() {
    Filter f;
    if (f.reading().valid || f.publish(0)) return false;
    for (unsigned i = 0; i < 19; ++i) f.sample(2000);
    if (f.publish(180000)) return false;
    f.sample(2000);
    if (!f.publish(190000) || !f.reading().valid) return false;
    batch(f, 2100);
    if (f.publish(1189999) || f.reading().adcMillivolts != 2000) return false;
    if (!f.publish(1190000) || f.reading().adcMillivolts != 2100) return false;
    return !f.publish(2190000);  // No batch, no stale publication.
}
static_assert(publicationCadence());

constexpr bool emaAndRecovery() {
    Filter f;
    batch(f, 2000); f.publish(0);
    batch(f, 2100); f.publish(1000000);
    if (!near(f.reading().voltage, packVoltage(2000, {}) +
        0.20f * (packVoltage(2100, {}) - packVoltage(2000, {})))) return false;
    batch(f, 0); f.publish(2000000);
    if (f.reading().valid || f.reading().low || f.reading().percent) return false;
    batch(f, 1900); f.publish(3000000);
    return f.reading().valid && near(f.reading().voltage, packVoltage(1900, {}));
}
static_assert(emaAndRecovery());

constexpr bool driverFailureCannotBeTrimmed() {
    Filter f;
    batch(f, 2000); f.publish(0);
    f.sample(0, false);
    for (unsigned i = 1; i < 20; ++i) f.sample(2000);
    f.publish(1000000);
    if (f.reading().valid) return false;
    batch(f, 2100); f.publish(2000000);
    return f.reading().valid && near(f.reading().voltage, packVoltage(2100, {}));
}
static_assert(driverFailureCannotBeTrimmed());

constexpr bool invalidClearsLowAndRecoveryReseeds() {
    Filter f;
    batch(f, 1800); f.publish(0);  // 10.67 V, low.
    if (!f.reading().low) return false;
    batch(f, 3300); f.publish(1000000);
    if (f.reading().valid || f.reading().low) return false;
    batch(f, 2000); f.publish(2000000);
    return f.reading().valid && !f.reading().low && near(f.reading().voltage, 11.856f);
}
static_assert(invalidClearsLowAndRecoveryReseeds());
