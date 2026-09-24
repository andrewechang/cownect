#pragma once

#include <cstdint>

namespace cownect::features {

// Compact DERIVED summary (Material 11 section 7). Logical object only - never sent by memcpy.
// CaptureSession remains the authoritative raw data. No behavior classification, no battery
// estimate (no battery-sense input exists on the PCB).
struct TelemetryRecord {
    uint8_t protocol_version;

    uint64_t device_id;
    uint32_t capture_id;

    uint64_t capture_start_us;  // monotonic; no fabricated absolute time

    bool gps_valid;
    int32_t latitude_e7;
    int32_t longitude_e7;

    bool board_temp_valid;
    float board_temp_c;

    bool cow_temp_valid;
    float cow_temp_c;

    bool accel_valid;
    float accel_magnitude_mean_g;
    float accel_magnitude_rms_g;
    float accel_magnitude_min_g;
    float accel_magnitude_max_g;

    bool microphone_valid;
    float microphone_mean_raw;
    float microphone_rms_ac_raw;
    uint16_t microphone_min_raw;
    uint16_t microphone_max_raw;

    uint32_t status_flags;
};

// Material 11 section 13.
enum TelemetryStatusFlag : uint32_t {
    TELEMETRY_STATUS_NONE             = 0,
    TELEMETRY_STATUS_CAPTURE_DEGRADED = 1u << 0,
    TELEMETRY_STATUS_ACCEL_ERROR      = 1u << 1,
    TELEMETRY_STATUS_GPS_NO_VALID_FIX = 1u << 2,
    TELEMETRY_STATUS_MIC_ERROR        = 1u << 3,
    TELEMETRY_STATUS_BOARD_TEMP_ERROR = 1u << 4,
    TELEMETRY_STATUS_COW_TEMP_ERROR   = 1u << 5,
    TELEMETRY_STATUS_COW_PROBE_FAULT  = 1u << 6,
    TELEMETRY_STATUS_ADC_ERROR        = 1u << 7,
    TELEMETRY_STATUS_CYCLE_OVERRUN    = 1u << 8,
};

}  // namespace cownect::features
