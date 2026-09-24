#pragma once

#include <cstdint>
#include "esp_adc/adc_cali.h"
#include "esp_err.h"
#include "hal/adc_types.h"

// Shared ADC1 board layer (Material 8 sections 6, 37; Material 9 section 6).
//
// All three analog inputs are on ADC1:
//   GPIO1 -> ADC1_CH0 board temperature
//   GPIO2 -> ADC1_CH1 cow thermistor
//   GPIO5 -> ADC1_CH4 microphone
// ESP-IDF forbids one ADC unit running continuous and one-shot at the same time, so every user
// must acquire ownership from the AdcResourceManager first.

namespace cownect::board {

enum class AnalogInput { BOARD_TEMP, COW_TEMP, MIC };

struct AdcChannelMap {
    adc_unit_t unit;
    adc_channel_t channel;
};

// Resolves the GPIO through ESP-IDF and verifies it matches the expected ADC1 channel.
esp_err_t adc_channel_for(AnalogInput input, AdcChannelMap& out);
int adc_gpio_for(AnalogInput input);
const char* analog_input_name(AnalogInput input);

// PROVISIONAL bring-up settings from menuconfig (same attenuation for all ADC1 paths).
adc_atten_t adc_attenuation();
const char* adc_attenuation_name();
adc_bitwidth_t adc_bitwidth();  // 12-bit on ESP32-S3
int adc_max_code();

// Near-rail classification from the configured code range (margin from menuconfig).
bool adc_code_near_low(int raw);
bool adc_code_near_high(int raw);

// ESP-IDF calibration (curve fitting on ESP32-S3). Created lazily once for the configured
// attenuation/bit width. Returns false if calibration is unavailable - callers must then mark
// millivolts invalid instead of inventing a value.
bool adc_raw_to_millivolts(int raw, int& millivolts_out);
bool adc_calibration_available();

enum class AdcMode { NONE, ONESHOT_SLOW_SENSORS, CONTINUOUS };
const char* adc_mode_name(AdcMode mode);

class AdcResourceManager {
public:
    // Returns COWNECT_ERR_RESOURCE_BUSY if ADC1 is owned in another mode.
    esp_err_t acquire(AdcMode mode);
    void release(AdcMode mode);
    AdcMode current_mode() const { return mode_; }

private:
    AdcMode mode_ = AdcMode::NONE;
    uint32_t refcount_ = 0;
};

AdcResourceManager& adc_resource();

}  // namespace cownect::board
