#pragma once

#include "capture_types.h"
#include "esp_err.h"

namespace cownect::data {

// Preallocated capture storage (Material 9 section 21). Allocated once at boot:
//   microphone raw ~1.93 MB in PSRAM, accelerometer/GPS/temperature arrays in internal RAM.
struct CaptureStorage {
    AccelSample* accel;
    size_t accel_capacity;
    uint16_t* mic;          // PSRAM, nullptr if allocation failed
    size_t mic_capacity;
    GpsFix* gps;
    size_t gps_capacity;
    BoardTemperatureSample* board_temp;
    CowTemperatureSample* cow_temp;
    size_t temp_capacity;
};

esp_err_t capture_storage_init();  // idempotent; ESP_ERR_NO_MEM if the PSRAM buffer failed
CaptureStorage& capture_storage();
bool capture_storage_microphone_available();

// The single CaptureSession bound to the storage (no double buffering - Material 13/14/15).
CaptureSession& capture_session();

// Clears counts/status and binds buffer pointers. Refuses while FROZEN_FOR_TRANSFER.
esp_err_t capture_session_reset(CaptureSession& s);

// Transfer ownership (Material 13 section 7).
void capture_session_freeze(CaptureSession& s);
void capture_session_release(CaptureSession& s);

// Explicit capture status bitfield (used by the capture stream header). Bit meanings:
enum CaptureStatusBit : uint32_t {
    CAPTURE_BIT_ACCEL_OK            = 1u << 0,
    CAPTURE_BIT_GPS_UART_OK         = 1u << 1,
    CAPTURE_BIT_GPS_HAD_VALID_FIX   = 1u << 2,
    CAPTURE_BIT_MIC_OK              = 1u << 3,
    CAPTURE_BIT_BOARD_TEMP_OK       = 1u << 4,
    CAPTURE_BIT_COW_TEMP_OK         = 1u << 5,
    CAPTURE_BIT_ADC_SHARED_OK       = 1u << 6,
    CAPTURE_BIT_ACCEL_DEGRADED      = 1u << 7,
    CAPTURE_BIT_MIC_DEGRADED        = 1u << 8,
    CAPTURE_BIT_GPS_DEGRADED        = 1u << 9,
    CAPTURE_BIT_COW_PROBE_FAULT     = 1u << 10,
};
uint32_t capture_status_bits(const CaptureStatus& st);

}  // namespace cownect::data
