#include "battery_monitor.h"

#include <cinttypes>
#include <cmath>
#include "battery_filter.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_timer.h"

namespace {
constexpr char kTag[] = "BATTERY";
constexpr adc_atten_t kAttenuation = ADC_ATTEN_DB_12;  // Value 3: Arduino ADC_11db.
adc_oneshot_unit_handle_t adc = nullptr;
adc_cali_handle_t cali = nullptr;
adc_channel_t channel{};
SpeakerBattery::Filter filter;
int64_t lastSampleUs = 0;
int64_t lastLogUs = -SpeakerBattery::kLogIntervalUs;
int64_t lastErrorUs = -SpeakerBattery::kLogIntervalUs;
uint32_t readErrors = 0;

esp_err_t releaseOnError(esp_err_t error) {
    if (cali) { adc_cali_delete_scheme_line_fitting(cali); cali = nullptr; }
    if (adc) { adc_oneshot_del_unit(adc); adc = nullptr; }
    return error;
}
}  // namespace

namespace BatteryMonitor {
esp_err_t begin(int adcPin, SpeakerBattery::Calibration calibration) {
    if (adc) return ESP_ERR_INVALID_STATE;
    if (!calibration.lowOhm || !std::isfinite(calibration.gain) || calibration.gain <= 0 ||
        !std::isfinite(calibration.offsetV)) return ESP_ERR_INVALID_ARG;
    adc_unit_t unit{};
    esp_err_t err = adc_oneshot_io_to_channel(adcPin, &unit, &channel);
    if (err != ESP_OK) return err;
    if (unit != ADC_UNIT_1) return ESP_ERR_INVALID_ARG;
    adc_oneshot_unit_init_cfg_t unitConfig{};
    unitConfig.unit_id = unit;
    err = adc_oneshot_new_unit(&unitConfig, &adc);
    if (err != ESP_OK) return err;
    adc_oneshot_chan_cfg_t channelConfig{};
    channelConfig.atten = kAttenuation;
    channelConfig.bitwidth = ADC_BITWIDTH_12;
    err = adc_oneshot_config_channel(adc, channel, &channelConfig);
    if (err != ESP_OK) return releaseOnError(err);

    adc_cali_line_fitting_efuse_val_t source{};
    err = adc_cali_scheme_line_fitting_check_efuse(&source);
    if (err != ESP_OK) return releaseOnError(err);
    adc_cali_line_fitting_config_t caliConfig{};
    caliConfig.unit_id = unit;
    caliConfig.atten = kAttenuation;
    caliConfig.bitwidth = ADC_BITWIDTH_12;
    caliConfig.default_vref = 1100;  // Same fallback as Arduino-ESP32 2.0.17.
    err = adc_cali_create_scheme_line_fitting(&caliConfig, &cali);
    if (err != ESP_OK) return releaseOnError(err);
    filter = SpeakerBattery::Filter(calibration);
    lastSampleUs = esp_timer_get_time() - SpeakerBattery::kSampleIntervalUs;
    ESP_LOGI(kTag, "ADC1 GPIO=%d channel=%d bits=12 attenuation=12dB; calibration=%s",
             adcPin, static_cast<int>(channel),
             source == ADC_CALI_LINE_FITTING_EFUSE_VAL_EFUSE_TP ? "eFuse two-point" :
             source == ADC_CALI_LINE_FITTING_EFUSE_VAL_EFUSE_VREF ? "eFuse Vref" : "default Vref 1100 mV");
    if (source == ADC_CALI_LINE_FITTING_EFUSE_VAL_DEFAULT_VREF) {
        ESP_LOGW(kTag, "Using default Vref; verify pack voltage against a multimeter");
    }
    ESP_LOGI(kTag, "Divider=%" PRIu32 "/%" PRIu32 " Ohm gain=%.6f offset=%.3f V; 20 samples, trim 2+2, EMA=0.20",
             calibration.highOhm, calibration.lowOhm, static_cast<double>(calibration.gain),
             static_cast<double>(calibration.offsetV));
    return ESP_OK;
}

bool update(int64_t nowUs) {
    if (!adc || !cali) return false;
    if (nowUs - lastSampleUs >= SpeakerBattery::kSampleIntervalUs) {
        lastSampleUs = nowUs;
        int mv = 0;
        const esp_err_t err = adc_oneshot_get_calibrated_result(adc, cali, channel, &mv);
        const bool ok = err == ESP_OK && mv >= 0;
        if (!ok) {
            ++readErrors;
            if (nowUs - lastErrorUs >= SpeakerBattery::kLogIntervalUs) {
                lastErrorUs = nowUs;
                ESP_LOGW(kTag, "ADC conversion failed: %s; invalidating batch", esp_err_to_name(err));
            }
        }
        filter.sample(ok ? static_cast<uint32_t>(mv) : 0, ok);
    }
    const auto previous = filter.reading();
    if (!filter.publish(nowUs)) return false;
    const auto value = filter.reading();
    if (value.valid && value.low && (!previous.valid || !previous.low)) {
        ESP_LOGW(kTag, "LOW BATTERY entered");
    } else if (value.valid && previous.valid && previous.low && !value.low) {
        ESP_LOGI(kTag, "LOW BATTERY cleared");
    }
    if (nowUs - lastLogUs >= SpeakerBattery::kLogIntervalUs) {
        lastLogUs = nowUs;
        if (value.valid) {
            ESP_LOGI(kTag, "adc=%" PRIu32 " mV pack=%.2f V percent=%u%% state=%s read_errors=%" PRIu32,
                     value.adcMillivolts, static_cast<double>(value.voltage), value.percent,
                     value.low ? "low" : "normal", readErrors);
        } else {
            ESP_LOGW(kTag, "adc=%" PRIu32 " mV pack=--.- V percent=-- state=invalid read_errors=%" PRIu32,
                     value.adcMillivolts, readErrors);
        }
    }
    return true;
}

SpeakerBattery::Reading reading() { return filter.reading(); }
}  // namespace BatteryMonitor
