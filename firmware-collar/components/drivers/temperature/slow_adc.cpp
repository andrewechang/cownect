#include "slow_adc.h"

#include "board_adc.h"
#include "cownect_config.h"
#include "esp_log.h"

namespace cownect::sensors {
namespace {
constexpr const char* TAG = "TEMP_ADC";
}

esp_err_t OneshotSlowAdc::init()
{
    if (unit_ != nullptr) {
        return ESP_OK;
    }
    board::AdcChannelMap board_map;
    board::AdcChannelMap cow_map;
    esp_err_t err = board::adc_channel_for(board::AnalogInput::BOARD_TEMP, board_map);
    if (err == ESP_OK) err = board::adc_channel_for(board::AnalogInput::COW_TEMP, cow_map);
    if (err != ESP_OK) {
        return err;
    }
    err = board::adc_resource().acquire(board::AdcMode::ONESHOT_SLOW_SENSORS);
    if (err != ESP_OK) {
        return err;
    }
    adc_oneshot_unit_init_cfg_t unit_cfg = {};
    unit_cfg.unit_id = ADC_UNIT_1;
    unit_cfg.ulp_mode = ADC_ULP_MODE_DISABLE;
    err = adc_oneshot_new_unit(&unit_cfg, &unit_);
    if (err != ESP_OK) {
        unit_ = nullptr;
        board::adc_resource().release(board::AdcMode::ONESHOT_SLOW_SENSORS);
        return err;
    }
    adc_oneshot_chan_cfg_t ch_cfg = {};
    ch_cfg.atten = board::adc_attenuation();
    ch_cfg.bitwidth = board::adc_bitwidth();
    board_channel_ = board_map.channel;
    cow_channel_ = cow_map.channel;
    err = adc_oneshot_config_channel(unit_, board_channel_, &ch_cfg);
    if (err == ESP_OK) err = adc_oneshot_config_channel(unit_, cow_channel_, &ch_cfg);
    if (err != ESP_OK) {
        deinit();
        return err;
    }
    ESP_LOGI(TAG, "one-shot ADC1 ready atten=%s (PROVISIONAL) calibration=%s", board::adc_attenuation_name(),
             board::adc_calibration_available() ? "available" : "UNAVAILABLE");
    return ESP_OK;
}

esp_err_t OneshotSlowAdc::deinit()
{
    if (unit_ == nullptr) {
        return ESP_OK;
    }
    esp_err_t err = adc_oneshot_del_unit(unit_);
    unit_ = nullptr;
    board::adc_resource().release(board::AdcMode::ONESHOT_SLOW_SENSORS);
    return err;
}

esp_err_t OneshotSlowAdc::read_channel(adc_channel_t channel, AdcReading& out)
{
    out = {};
    if (unit_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    const uint32_t n = config::TEMP_ONESHOT_AVERAGE_COUNT;
    int64_t sum = 0;
    for (uint32_t i = 0; i < n; ++i) {
        int raw = 0;
        esp_err_t err = adc_oneshot_read(unit_, channel, &raw);
        if (err != ESP_OK) {
            return err;
        }
        sum += raw;
    }
    out.raw = static_cast<int>((sum + n / 2) / n);
    out.millivolts_valid = board::adc_raw_to_millivolts(out.raw, out.millivolts);
    if (!out.millivolts_valid) {
        out.millivolts = 0;
    }
    return ESP_OK;
}

esp_err_t OneshotSlowAdc::read_board_temperature_channel(AdcReading& out)
{
    return read_channel(board_channel_, out);
}

esp_err_t OneshotSlowAdc::read_cow_temperature_channel(AdcReading& out)
{
    return read_channel(cow_channel_, out);
}

}  // namespace cownect::sensors
