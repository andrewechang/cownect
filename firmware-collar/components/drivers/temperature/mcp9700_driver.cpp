#include "board_temperature.h"

#include <cmath>
#include "temperature_math.h"

namespace cownect::sensors {
namespace {
// MCP9700A specified temperature range (datasheet): -40 .. +125 deg C.
constexpr float kSpecMinC = -40.0f;
constexpr float kSpecMaxC = 125.0f;
}  // namespace

esp_err_t Mcp9700Driver::init()
{
    return adc_->init();
}

void Mcp9700Driver::convert(const AdcReading& reading, BoardTemperatureSample& out)
{
    out.adc_raw = reading.raw;
    out.millivolts = reading.millivolts;
    out.millivolts_valid = reading.millivolts_valid;
    out.temperature_c = NAN;
    if (!reading.millivolts_valid) {
        out.status = TemperatureStatus::ADC_CALIBRATION_UNAVAILABLE;
        return;
    }
    const float c = mcp9700_mv_to_celsius(static_cast<float>(reading.millivolts));
    if (!std::isfinite(c) || c < kSpecMinC || c > kSpecMaxC) {
        out.status = TemperatureStatus::INVALID_VOLTAGE;  // outside the sensor's specified output range
        return;
    }
    out.temperature_c = c;
    out.status = TemperatureStatus::OK;
}

esp_err_t Mcp9700Driver::sample(BoardTemperatureSample& out)
{
    const uint32_t ts = out.timestamp_ms;
    out = {};
    out.timestamp_ms = ts;
    out.temperature_c = NAN;
    AdcReading r;
    esp_err_t err = adc_->read_board_temperature_channel(r);
    if (err != ESP_OK) {
        out.status = TemperatureStatus::ADC_ERROR;
        return err;
    }
    convert(r, out);
    return ESP_OK;
}

}  // namespace cownect::sensors
