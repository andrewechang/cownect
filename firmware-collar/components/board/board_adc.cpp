#include "board_adc.h"

#include "board_pins.h"
#include "cownect_config.h"
#include "cownect_err.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

namespace cownect::board {
namespace {
constexpr const char* TAG = "ADC";
portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
adc_cali_handle_t s_cali = nullptr;
bool s_cali_attempted = false;
}  // namespace

int adc_gpio_for(AnalogInput input)
{
    switch (input) {
    case AnalogInput::BOARD_TEMP: return PIN_BOARD_TEMP_ADC;
    case AnalogInput::COW_TEMP:   return PIN_COW_TEMP_ADC;
    case AnalogInput::MIC:        return PIN_MIC_ADC;
    }
    return -1;
}

const char* analog_input_name(AnalogInput input)
{
    switch (input) {
    case AnalogInput::BOARD_TEMP: return "BOARD_TEMP";
    case AnalogInput::COW_TEMP:   return "COW_TEMP";
    case AnalogInput::MIC:        return "MIC";
    }
    return "?";
}

esp_err_t adc_channel_for(AnalogInput input, AdcChannelMap& out)
{
    adc_unit_t unit;
    adc_channel_t channel;
    esp_err_t err = adc_oneshot_io_to_channel(adc_gpio_for(input), &unit, &channel);
    if (err != ESP_OK) {
        return err;
    }
    // Expected ESP32-S3 mapping (Material 8 section 6).
    adc_channel_t expected = ADC_CHANNEL_0;
    switch (input) {
    case AnalogInput::BOARD_TEMP: expected = ADC_CHANNEL_0; break;
    case AnalogInput::COW_TEMP:   expected = ADC_CHANNEL_1; break;
    case AnalogInput::MIC:        expected = ADC_CHANNEL_4; break;
    }
    if (unit != ADC_UNIT_1 || channel != expected) {
        ESP_LOGE(TAG, "%s GPIO%d maps to unit %d ch %d, expected ADC1 ch %d",
                 analog_input_name(input), adc_gpio_for(input), unit, channel, expected);
        return ESP_ERR_INVALID_STATE;
    }
    out.unit = unit;
    out.channel = channel;
    return ESP_OK;
}

adc_atten_t adc_attenuation()
{
#if CONFIG_COWNECT_ADC_ATTEN_DB_0
    return ADC_ATTEN_DB_0;
#elif CONFIG_COWNECT_ADC_ATTEN_DB_2_5
    return ADC_ATTEN_DB_2_5;
#elif CONFIG_COWNECT_ADC_ATTEN_DB_6
    return ADC_ATTEN_DB_6;
#else
    return ADC_ATTEN_DB_12;
#endif
}

const char* adc_attenuation_name()
{
    switch (adc_attenuation()) {
    case ADC_ATTEN_DB_0:   return "0dB";
    case ADC_ATTEN_DB_2_5: return "2.5dB";
    case ADC_ATTEN_DB_6:   return "6dB";
    case ADC_ATTEN_DB_12:  return "12dB";
    default:               return "?";
    }
}

adc_bitwidth_t adc_bitwidth()
{
    return ADC_BITWIDTH_12;
}

int adc_max_code()
{
    return (1 << 12) - 1;
}

bool adc_code_near_low(int raw)
{
    return raw <= config::ADC_NEAR_RAIL_MARGIN_CODES;
}

bool adc_code_near_high(int raw)
{
    return raw >= adc_max_code() - config::ADC_NEAR_RAIL_MARGIN_CODES;
}

static void ensure_calibration()
{
    if (s_cali_attempted) {
        return;
    }
    s_cali_attempted = true;
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cfg = {};
    cfg.unit_id = ADC_UNIT_1;
    cfg.atten = adc_attenuation();
    cfg.bitwidth = adc_bitwidth();
    esp_err_t err = adc_cali_create_scheme_curve_fitting(&cfg, &s_cali);
    if (err != ESP_OK) {
        s_cali = nullptr;
        ESP_LOGW(TAG, "calibration unavailable (%s) - millivolts will be marked invalid", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "calibration: curve fitting, atten=%s (PROVISIONAL)", adc_attenuation_name());
    }
#else
    ESP_LOGW(TAG, "no supported calibration scheme - millivolts will be marked invalid");
#endif
}

bool adc_calibration_available()
{
    ensure_calibration();
    return s_cali != nullptr;
}

bool adc_raw_to_millivolts(int raw, int& millivolts_out)
{
    ensure_calibration();
    if (s_cali == nullptr) {
        return false;
    }
    return adc_cali_raw_to_voltage(s_cali, raw, &millivolts_out) == ESP_OK;
}

const char* adc_mode_name(AdcMode mode)
{
    switch (mode) {
    case AdcMode::NONE:                 return "NONE";
    case AdcMode::ONESHOT_SLOW_SENSORS: return "ONESHOT";
    case AdcMode::CONTINUOUS:           return "CONTINUOUS";
    }
    return "?";
}

esp_err_t AdcResourceManager::acquire(AdcMode mode)
{
    if (mode == AdcMode::NONE) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t result = ESP_OK;
    taskENTER_CRITICAL(&s_lock);
    if (mode_ == AdcMode::NONE) {
        mode_ = mode;
        refcount_ = 1;
    } else if (mode_ == mode && mode == AdcMode::ONESHOT_SLOW_SENSORS) {
        ++refcount_;  // several one-shot users may share the one-shot unit
    } else {
        result = COWNECT_ERR_RESOURCE_BUSY;
    }
    taskEXIT_CRITICAL(&s_lock);
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "ADC1 ownership refused: requested %s, owned by %s", adc_mode_name(mode), adc_mode_name(mode_));
    }
    return result;
}

void AdcResourceManager::release(AdcMode mode)
{
    taskENTER_CRITICAL(&s_lock);
    if (mode_ == mode && refcount_ > 0) {
        if (--refcount_ == 0) {
            mode_ = AdcMode::NONE;
        }
    }
    taskEXIT_CRITICAL(&s_lock);
}

AdcResourceManager& adc_resource()
{
    static AdcResourceManager instance;
    return instance;
}

}  // namespace cownect::board
