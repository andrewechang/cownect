#include "temperature_math.h"

#include <cmath>
#include "cownect_config.h"
#include "temperature_types.h"

namespace cownect::sensors {

float mcp9700_mv_to_celsius(float millivolts)
{
    return (millivolts - config::MCP9700_V0_MV) / config::MCP9700_SLOPE_MV_PER_C;
}

bool ntc_divider_resistance(float node_mv, float excitation_mv, float fixed_resistor_ohm, float& resistance_ohm)
{
    if (!std::isfinite(node_mv) || !std::isfinite(excitation_mv) || !std::isfinite(fixed_resistor_ohm)) {
        return false;
    }
    if (excitation_mv <= 0.0f || fixed_resistor_ohm <= 0.0f) {
        return false;
    }
    if (node_mv <= 0.0f || node_mv >= excitation_mv) {
        return false;
    }
    const float r = fixed_resistor_ohm * node_mv / (excitation_mv - node_mv);
    if (!std::isfinite(r) || r <= 0.0f) {
        return false;
    }
    resistance_ohm = r;
    return true;
}

bool ntc_beta_resistance_to_celsius(float resistance_ohm, float r25_ohm, float beta_k, float& celsius)
{
    if (!std::isfinite(resistance_ohm) || resistance_ohm <= 0.0f || r25_ohm <= 0.0f || beta_k <= 0.0f) {
        return false;
    }
    constexpr double kT25 = 298.15;
    const double inv_t = 1.0 / kT25 + std::log(static_cast<double>(resistance_ohm) / r25_ohm) / beta_k;
    if (!std::isfinite(inv_t) || inv_t <= 0.0) {
        return false;
    }
    const double c = 1.0 / inv_t - 273.15;
    if (!std::isfinite(c)) {
        return false;
    }
    celsius = static_cast<float>(c);
    return true;
}

const char* temperature_status_name(TemperatureStatus s)
{
    switch (s) {
    case TemperatureStatus::OK:                          return "OK";
    case TemperatureStatus::ADC_ERROR:                   return "ADC_ERROR";
    case TemperatureStatus::ADC_CALIBRATION_UNAVAILABLE: return "CAL_UNAVAILABLE";
    case TemperatureStatus::INVALID_VOLTAGE:             return "INVALID_VOLTAGE";
    case TemperatureStatus::INVALID_RESISTANCE:          return "INVALID_RESISTANCE";
    case TemperatureStatus::SENSOR_OPEN_SUSPECTED:       return "OPEN_SUSPECTED";
    case TemperatureStatus::SENSOR_SHORT_SUSPECTED:      return "SHORT_SUSPECTED";
    case TemperatureStatus::CONVERSION_ERROR:            return "CONVERSION_ERROR";
    }
    return "?";
}

void temperature_account_board(TemperatureCaptureStats& st, const BoardTemperatureSample& s)
{
    st.board_samples_attempted++;
    if (s.status == TemperatureStatus::OK) {
        st.board_samples_valid++;
    } else if (s.status == TemperatureStatus::ADC_ERROR) {
        st.board_adc_errors++;
    } else {
        st.board_conversion_errors++;
    }
}

void temperature_account_cow(TemperatureCaptureStats& st, const CowTemperatureSample& s)
{
    st.cow_samples_attempted++;
    switch (s.status) {
    case TemperatureStatus::OK:                     st.cow_samples_valid++; break;
    case TemperatureStatus::ADC_ERROR:              st.cow_adc_errors++; break;
    case TemperatureStatus::SENSOR_OPEN_SUSPECTED:  st.cow_open_suspected++; break;
    case TemperatureStatus::SENSOR_SHORT_SUSPECTED: st.cow_short_suspected++; break;
    default:                                        st.cow_conversion_errors++; break;
    }
}

}  // namespace cownect::sensors
