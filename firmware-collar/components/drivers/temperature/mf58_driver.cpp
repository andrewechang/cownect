#include "cow_temperature.h"

#include <cmath>
#include "board_adc.h"
#include "cownect_config.h"

namespace cownect::sensors {

const IThermistorModel& mf58_default_model()
{
    static const BetaThermistorModel model(config::MF58_R25_OHM, config::MF58_BETA_K);
    return model;
}

ThermistorElectricalConfig mf58_default_electrical()
{
    ThermistorElectricalConfig c;
    c.fixed_resistor_ohm = config::MF58_FIXED_R_OHM;
    c.excitation_mv = config::MF58_NOMINAL_EXCITATION_MV;
    c.excitation_is_nominal = true;
    return c;
}

esp_err_t Mf58Driver::init()
{
    return adc_->init();
}

void Mf58Driver::convert(const AdcReading& reading, CowTemperatureSample& out) const
{
    out.adc_raw = reading.raw;
    out.millivolts = reading.millivolts;
    out.millivolts_valid = reading.millivolts_valid;
    out.resistance_ohm = NAN;
    out.temperature_c = NAN;

    // Rail checks from the configured ADC code range (open -> node pulled toward SWITCHED_3V3,
    // short -> node pulled toward GND). Saturation at the top rail is accepted as open-suspected.
    if (board::adc_code_near_high(reading.raw)) {
        out.status = TemperatureStatus::SENSOR_OPEN_SUSPECTED;
        return;
    }
    if (board::adc_code_near_low(reading.raw)) {
        out.status = TemperatureStatus::SENSOR_SHORT_SUSPECTED;
        return;
    }
    if (!reading.millivolts_valid) {
        out.status = TemperatureStatus::ADC_CALIBRATION_UNAVAILABLE;
        return;
    }
    // Optional validated voltage thresholds (0 = CONFIG_NOT_SET).
    if (config::COW_OPEN_THRESHOLD_MV > 0 && reading.millivolts >= config::COW_OPEN_THRESHOLD_MV) {
        out.status = TemperatureStatus::SENSOR_OPEN_SUSPECTED;
        return;
    }
    if (config::COW_SHORT_THRESHOLD_MV > 0 && reading.millivolts <= config::COW_SHORT_THRESHOLD_MV) {
        out.status = TemperatureStatus::SENSOR_SHORT_SUSPECTED;
        return;
    }
    float r = 0.0f;
    if (!ntc_divider_resistance(static_cast<float>(reading.millivolts), electrical_.excitation_mv,
                                electrical_.fixed_resistor_ohm, r)) {
        // Vnode <= 0 or Vnode >= Vexc: no division, no plausible fake temperature.
        out.status = TemperatureStatus::INVALID_VOLTAGE;
        return;
    }
    out.resistance_ohm = r;
    float c = 0.0f;
    if (!model_->resistance_to_celsius(r, c)) {
        out.status = TemperatureStatus::CONVERSION_ERROR;
        return;
    }
    out.temperature_c = c;
    out.status = TemperatureStatus::OK;
}

esp_err_t Mf58Driver::sample(CowTemperatureSample& out)
{
    const uint32_t ts = out.timestamp_ms;
    out = {};
    out.timestamp_ms = ts;
    out.resistance_ohm = NAN;
    out.temperature_c = NAN;
    AdcReading r;
    esp_err_t err = adc_->read_cow_temperature_channel(r);
    if (err != ESP_OK) {
        out.status = TemperatureStatus::ADC_ERROR;
        return err;
    }
    convert(r, out);
    return ESP_OK;
}

}  // namespace cownect::sensors
