#pragma once

#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"

namespace cownect::sensors {

struct AdcReading {
    int raw;
    int millivolts;
    bool millivolts_valid;  // false when ESP-IDF calibration is unavailable - never invented
};

// Slow analog provider for the temperature drivers (Material 6 section 7).
// Standalone: OneshotSlowAdc. Integrated (Material 9): 1 s aggregates from the continuous engine.
class IAdcSlowSampler {
public:
    virtual ~IAdcSlowSampler() = default;
    virtual esp_err_t init() = 0;
    virtual esp_err_t read_board_temperature_channel(AdcReading& out) = 0;
    virtual esp_err_t read_cow_temperature_channel(AdcReading& out) = 0;
};

// ADC1 one-shot backend. Owns ADC1 in ONESHOT mode while initialized: it refuses to start while
// the continuous microphone/integrated engine owns ADC1 (Material 8 section 36).
class OneshotSlowAdc final : public IAdcSlowSampler {
public:
    esp_err_t init() override;
    esp_err_t deinit();
    esp_err_t read_board_temperature_channel(AdcReading& out) override;
    esp_err_t read_cow_temperature_channel(AdcReading& out) override;
    bool initialized() const { return unit_ != nullptr; }

private:
    esp_err_t read_channel(adc_channel_t channel, AdcReading& out);

    adc_oneshot_unit_handle_t unit_ = nullptr;
    adc_channel_t board_channel_ = ADC_CHANNEL_0;
    adc_channel_t cow_channel_ = ADC_CHANNEL_1;
};

}  // namespace cownect::sensors
