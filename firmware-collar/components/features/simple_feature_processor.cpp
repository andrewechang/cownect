#include "feature_processor.h"

#include <cmath>
#include "accelerometer_decode.h"
#include "cownect_config.h"

namespace cownect::features {

using sensors::TemperatureStatus;

MagnitudeSummary summarize_accel_magnitude(const data::AccelSample* samples, size_t count)
{
    MagnitudeSummary m = {};
    if (samples == nullptr || count == 0) {
        return m;
    }
    double sum = 0.0;
    double sum_sq = 0.0;
    float mn = INFINITY;
    float mx = -INFINITY;
    for (size_t i = 0; i < count; ++i) {
        // Conversion helper is NEEDS_HARDWARE_VALIDATION (Material 5 orientation test).
        const float g = sensors::accel_magnitude_g(samples[i]);
        sum += g;
        sum_sq += static_cast<double>(g) * g;
        if (g < mn) mn = g;
        if (g > mx) mx = g;
    }
    m.valid = true;
    m.mean_g = static_cast<float>(sum / count);
    m.rms_g = static_cast<float>(std::sqrt(sum_sq / count));
    m.min_g = mn;
    m.max_g = mx;
    return m;
}

int latest_valid_gps(const data::GpsFix* fixes, size_t count)
{
    for (size_t i = count; i-- > 0;) {
        if (fixes[i].valid) return static_cast<int>(i);
    }
    return -1;
}

int latest_valid_board_temp(const data::BoardTemperatureSample* s, size_t count)
{
    for (size_t i = count; i-- > 0;) {
        if (s[i].status == TemperatureStatus::OK) return static_cast<int>(i);
    }
    return -1;
}

int latest_valid_cow_temp(const data::CowTemperatureSample* s, size_t count)
{
    for (size_t i = count; i-- > 0;) {
        if (s[i].status == TemperatureStatus::OK) return static_cast<int>(i);
    }
    return -1;
}

uint32_t telemetry_status_flags(const data::CaptureSession& c)
{
    const data::CaptureStatus& st = c.status;
    uint32_t f = TELEMETRY_STATUS_NONE;
    if (!st.accel_ok) f |= TELEMETRY_STATUS_ACCEL_ERROR;
    if (!st.gps_had_valid_fix) f |= TELEMETRY_STATUS_GPS_NO_VALID_FIX;
    if (!st.microphone_ok) f |= TELEMETRY_STATUS_MIC_ERROR;
    if (!st.board_temp_ok) f |= TELEMETRY_STATUS_BOARD_TEMP_ERROR;
    if (!st.cow_temp_ok) f |= TELEMETRY_STATUS_COW_TEMP_ERROR;
    if (st.cow_probe_fault) f |= TELEMETRY_STATUS_COW_PROBE_FAULT;
    if (!st.adc_shared_ok) f |= TELEMETRY_STATUS_ADC_ERROR;
    const bool degraded = st.accel_degraded || st.microphone_degraded || st.gps_degraded || !st.accel_ok ||
                          !st.gps_ok || !st.microphone_ok || !st.adc_shared_ok;
    if (degraded) f |= TELEMETRY_STATUS_CAPTURE_DEGRADED;
    return f;
}

esp_err_t SimpleFeatureProcessor::process(const data::CaptureSession& c, TelemetryRecord& out)
{
    out = {};
    if (c.state != data::CaptureState::COMPLETE && c.state != data::CaptureState::FROZEN_FOR_TRANSFER) {
        return ESP_ERR_INVALID_STATE;
    }
    if (c.id.capture_id == 0) {
        return ESP_ERR_INVALID_ARG;  // corrupted identity
    }
    out.protocol_version = config::TELEMETRY_PROTOCOL_VERSION;
    out.device_id = c.id.device_id;
    out.capture_id = c.id.capture_id;  // never a second independent ID
    out.capture_start_us = c.capture_start_us;

    const int g = latest_valid_gps(c.gps_fixes, c.gps_fix_count);
    if (g >= 0) {
        out.gps_valid = true;
        out.latitude_e7 = c.gps_fixes[g].latitude_e7;
        out.longitude_e7 = c.gps_fixes[g].longitude_e7;
    }
    const int b = latest_valid_board_temp(c.board_temperature_samples, c.board_temperature_count);
    if (b >= 0) {
        out.board_temp_valid = true;
        out.board_temp_c = c.board_temperature_samples[b].temperature_c;
    }
    const int w = latest_valid_cow_temp(c.cow_temperature_samples, c.cow_temperature_count);
    if (w >= 0) {
        out.cow_temp_valid = true;
        out.cow_temp_c = c.cow_temperature_samples[w].temperature_c;
    }
    if (c.status.accel_ok) {
        const MagnitudeSummary m = summarize_accel_magnitude(c.accel_samples, c.accel_count);
        out.accel_valid = m.valid;
        out.accel_magnitude_mean_g = m.mean_g;
        out.accel_magnitude_rms_g = m.rms_g;
        out.accel_magnitude_min_g = m.min_g;
        out.accel_magnitude_max_g = m.max_g;
    }
    // Reuse capture-time statistics; the ~1.9 MB buffer is not rescanned or copied.
    const auto& mic = c.diagnostics.microphone;
    if (c.status.microphone_ok && mic.statistics_valid) {
        out.microphone_valid = true;
        out.microphone_mean_raw = static_cast<float>(mic.raw_mean);
        out.microphone_rms_ac_raw = static_cast<float>(mic.raw_rms_ac);
        out.microphone_min_raw = mic.raw_min;
        out.microphone_max_raw = mic.raw_max;
    }
    out.status_flags = telemetry_status_flags(c);
    return ESP_OK;
}

}  // namespace cownect::features
