#pragma once

#include <cstddef>
#include <cstdint>
#include "accelerometer_types.h"
#include "analog_adc_types.h"
#include "esp_err.h"
#include "gps_types.h"
#include "microphone_types.h"
#include "temperature_types.h"

namespace cownect::data {

using sensors::AccelSample;
using sensors::BoardTemperatureSample;
using sensors::CowTemperatureSample;
using sensors::GpsFix;

// Stable identity shared by CaptureSession, TelemetryRecord, LoRa and Wi-Fi (Material 3 section 14).
struct CaptureIdentity {
    uint64_t device_id;
    uint32_t capture_id;
};

// Per-subsystem status - no single boolean hides partial success (Material 9 section 24).
struct CaptureStatus {
    bool accel_ok;
    bool gps_ok;              // GPS UART produced data during the capture
    bool gps_had_valid_fix;
    bool microphone_ok;
    bool board_temp_ok;       // at least one valid logical sample
    bool cow_temp_ok;         // at least one valid logical sample
    bool adc_shared_ok;       // shared ADC1 engine started and stopped cleanly

    bool accel_degraded;      // FIFO overrun / dropped / I2C errors
    bool microphone_degraded; // pool/destination overflow or ADC errors
    bool gps_degraded;        // UART overflow
    bool cow_probe_fault;     // open/short suspected in any sample

    uint32_t mic_overflow_count;
    uint32_t gps_uart_overflow_count;

    esp_err_t accel_error;
    esp_err_t gps_error;
    esp_err_t analog_error;
};

// Material 9 section 36.
struct CaptureDiagnostics {
    sensors::IntegratedAdcStats adc;
    sensors::MicrophoneCaptureStats microphone;
    sensors::AccelerometerCaptureStats accelerometer;
    sensors::GpsCaptureStats gps;
    sensors::TemperatureCaptureStats temperature;

    uint64_t requested_capture_us;
    uint64_t actual_capture_us;
    uint32_t sensor_manager_service_overrun_count;
};

enum class CaptureState : uint8_t {
    EMPTY,
    CAPTURING,
    COMPLETE,
    FROZEN_FOR_TRANSFER,  // buffers owned by a communication transfer; no new capture allowed
};

// In-memory capture model (Material 9 section 20). NOT a wire format: communication modules
// always use the explicit CaptureStreamEncoder.
struct CaptureSession {
    CaptureIdentity id;

    uint64_t cycle_start_us;
    uint64_t capture_start_us;
    uint64_t capture_end_us;

    AccelSample* accel_samples;
    size_t accel_count;

    uint16_t* microphone_raw_samples;
    size_t microphone_sample_count;
    uint32_t microphone_sample_rate_hz;

    GpsFix* gps_fixes;
    size_t gps_fix_count;

    BoardTemperatureSample* board_temperature_samples;
    size_t board_temperature_count;

    CowTemperatureSample* cow_temperature_samples;
    size_t cow_temperature_count;

    CaptureStatus status;
    CaptureDiagnostics diagnostics;
    CaptureState state;
};

}  // namespace cownect::data
