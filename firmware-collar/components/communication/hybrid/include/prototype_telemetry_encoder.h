#pragma once

#include <cstddef>
#include <cstdint>
#include "esp_err.h"
#include "telemetry_record.h"

namespace cownect::hybrid {

// Prototype LoRa telemetry packet, version 1 (Material 15 section 8). 49 bytes, little-endian:
//   u16 magic (0x4D43, bytes "CM") | u8 protocol_version (1) | u8 packet_type (0x10 TELEMETRY) |
//   u64 device_id | u32 capture_id | u8 valid_flags |
//   i32 latitude_e7 | i32 longitude_e7 | f32 board_temp_c | f32 cow_temp_c |
//   f32 accel_magnitude_mean_g | f32 accel_magnitude_rms_g | f32 microphone_rms_ac_raw |
//   u32 status_flags
// valid_flags: bit0 gps, bit1 board_temp, bit2 cow_temp, bit3 accel, bit4 microphone.
// A field whose valid bit is clear is encoded as 0 and MUST be ignored by the decoder.
inline constexpr uint16_t TELEMETRY_MAGIC = 0x4D43;
inline constexpr uint8_t TELEMETRY_PACKET_VERSION = 1;
inline constexpr uint8_t TELEMETRY_PACKET_TYPE = 0x10;
inline constexpr size_t TELEMETRY_PACKET_BYTES = 49;

enum TelemetryValidBit : uint8_t {
    TEL_VALID_GPS = 1u << 0,
    TEL_VALID_BOARD_TEMP = 1u << 1,
    TEL_VALID_COW_TEMP = 1u << 2,
    TEL_VALID_ACCEL = 1u << 3,
    TEL_VALID_MIC = 1u << 4,
};

class PrototypeTelemetryEncoder {
public:
    esp_err_t encode(const features::TelemetryRecord& record, uint8_t* dst, size_t dst_capacity,
                     size_t& bytes_written) const;
};

// Reference decoder (same schema as the Heltec gateway / Jetson; used by unit tests).
esp_err_t prototype_telemetry_decode(const uint8_t* src, size_t length, features::TelemetryRecord& out);

}  // namespace cownect::hybrid
