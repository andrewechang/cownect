#pragma once

// Central CowNect configuration (Material 3 section 24, Material 4 section 17).
//
// Three kinds of values live here:
//   1. Values explicitly approved by the Materials (cycle, capture, sensor rates...).
//   2. DEVELOPMENT DEFAULTs approved by the user, exposed through menuconfig and marked
//      NEEDS_HARDWARE_VALIDATION.
//   3. Unresolved values (RF profile, Wi-Fi, Jetson): they are NOT here. They are read from
//      menuconfig strings/ints that default to CONFIG_NOT_SET and are validated at runtime.

#include <cstddef>
#include <cstdint>
#include "sdkconfig.h"

namespace cownect::config {

inline constexpr const char* FIRMWARE_VERSION = "0.1.0-proto";
inline constexpr const char* PINNED_IDF_VERSION = "v5.5.5";

// ---- Timing (Material 3 / 10) ------------------------------------------------------------
inline constexpr uint32_t CYCLE_TARGET_MS = 120000;
inline constexpr uint32_t CAPTURE_TIME_MS = 30000;

// ---- Accelerometer (Material 5) ----------------------------------------------------------
inline constexpr uint8_t  LIS2DW12_I2C_ADDR = 0x18;
inline constexpr uint8_t  LIS2DW12_WHO_AM_I_VALUE = 0x44;
inline constexpr uint32_t ACCEL_ODR_HZ = 50;
inline constexpr int      ACCEL_FULL_SCALE_G = 4;
inline constexpr uint32_t ACCEL_FIFO_POLL_MS = 100;
inline constexpr uint8_t  ACCEL_FIFO_THRESHOLD = 16;
// Material 5 section 25 suggests ~1510-1520 records to avoid artificial boundary overflow.
inline constexpr size_t   ACCEL_CAPTURE_MAX_SAMPLES = 1520;
inline constexpr uint32_t ACCEL_I2C_TIMEOUT_MS = 50;  // per-transaction bound (design value)
inline constexpr uint32_t I2C_CLOCK_HZ = CONFIG_COWNECT_I2C_CLOCK_HZ;

// ---- GPS (Material 7) --------------------------------------------------------------------
inline constexpr uint32_t GPS_BAUD = 9600;
inline constexpr uint32_t GPS_UPDATE_HZ = 1;
inline constexpr size_t   GPS_CAPTURE_MAX_FIXES = 40;
inline constexpr size_t   GPS_UART_RX_BUFFER_BYTES = 4096;
inline constexpr int      GPS_UART_PORT = CONFIG_COWNECT_GPS_UART_PORT;
inline constexpr size_t   GPS_MAX_SENTENCE_BYTES = CONFIG_COWNECT_GPS_MAX_SENTENCE_BYTES;

// ---- Temperature (Material 6) ------------------------------------------------------------
inline constexpr uint32_t TEMP_SAMPLE_HZ = 1;
inline constexpr size_t   TEMP_CAPTURE_MAX_SAMPLES = 32;
inline constexpr float    MCP9700_V0_MV = 500.0f;
inline constexpr float    MCP9700_SLOPE_MV_PER_C = 10.0f;
inline constexpr float    MF58_R25_OHM = 10000.0f;
inline constexpr float    MF58_BETA_K = 3950.0f;
inline constexpr float    MF58_FIXED_R_OHM = 10000.0f;          // R12
inline constexpr float    MF58_NOMINAL_EXCITATION_MV = 3300.0f;  // NOMINAL, not measured
inline constexpr uint32_t TEMP_ONESHOT_AVERAGE_COUNT = CONFIG_COWNECT_TEMP_ONESHOT_AVERAGE_COUNT;
inline constexpr int32_t  COW_OPEN_THRESHOLD_MV = CONFIG_COWNECT_COW_OPEN_THRESHOLD_MV;   // 0 = not set
inline constexpr int32_t  COW_SHORT_THRESHOLD_MV = CONFIG_COWNECT_COW_SHORT_THRESHOLD_MV; // 0 = not set

// ---- Microphone / ADC (Material 8 / 9) ---------------------------------------------------
inline constexpr uint32_t MIC_SAMPLE_RATE_HZ = 32000;
inline constexpr uint32_t MIC_CONTAINER_BITS = 16;
inline constexpr size_t   MIC_EXPECTED_SAMPLES = 960000;
// Small capacity margin (0.1 s at 32 kS/s) so start/stop phase does not cause artificial
// destination overflow (Material 8 section 14). Design value.
inline constexpr size_t   MIC_CAPTURE_MAX_SAMPLES = MIC_EXPECTED_SAMPLES + 3200;
inline constexpr uint32_t INTEGRATED_ADC_TOTAL_HZ = 64000;  // Material 9 section 4
inline constexpr uint32_t ADC_CONV_FRAME_BYTES = CONFIG_COWNECT_ADC_CONV_FRAME_BYTES;
inline constexpr uint32_t ADC_POOL_BYTES = CONFIG_COWNECT_ADC_POOL_BYTES;
inline constexpr uint32_t ADC_READ_TIMEOUT_MS = 20;   // bounded frame wait (design value)
inline constexpr int      ADC_NEAR_RAIL_MARGIN_CODES = CONFIG_COWNECT_ADC_NEAR_RAIL_MARGIN_CODES;
inline constexpr uint32_t TEMP_BUCKET_MS = 1000 / TEMP_SAMPLE_HZ;

// ---- Board / development defaults ----------------------------------------------------------
inline constexpr uint64_t DEVELOPMENT_DEVICE_ID = CONFIG_COWNECT_DEVICE_ID;
inline constexpr uint32_t PERIPH_STABILIZE_MS = CONFIG_COWNECT_PERIPH_STABILIZE_MS;
inline constexpr uint32_t MODE_DEBOUNCE_MS = CONFIG_COWNECT_MODE_DEBOUNCE_MS;

// ---- Communication -------------------------------------------------------------------------
inline constexpr uint32_t LORA_REPORT_INTERVAL_SEC = 10;          // Material 3 (not used by M15 flow)
inline constexpr bool     LORA_TX_DURING_CAPTURE_DEFAULT = false; // Material 3 / 12
inline constexpr uint8_t  TELEMETRY_PROTOCOL_VERSION = 1;         // Material 11
inline constexpr size_t   SX126X_MAX_PAYLOAD_BYTES = 255;         // SX126x payload length is 8-bit
inline constexpr size_t   UPLOAD_CHUNK_BYTES = CONFIG_COWNECT_UPLOAD_CHUNK_BYTES;

}  // namespace cownect::config
