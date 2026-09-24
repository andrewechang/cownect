#pragma once

#include <cstdint>

// CowNect capture stream, version 1 (Material 13 sections 11-20). All fields little-endian.
// This file is the normative byte layout shared with the Jetson/Heltec decoders
// (see docs/protocols.md).
namespace cownect::codec::stream {

inline constexpr uint32_t CAPTURE_STREAM_MAGIC = 0x53434E43u;  // bytes "CNCS"
inline constexpr uint8_t CAPTURE_STREAM_VERSION = 1;

// Header (68 bytes):
//  u32 magic | u8 stream_version | u8 reserved0 | u16 header_bytes | u64 device_id |
//  u32 capture_id | u64 capture_start_us | u64 capture_end_us | u32 total_stream_bytes |
//  u32 accel_count | u32 gps_count | u32 board_temp_count | u32 cow_temp_count |
//  u32 microphone_count | u32 microphone_sample_rate_hz | u32 capture_status_flags
inline constexpr uint16_t HEADER_BYTES = 68;

// Section header (12 bytes): u8 type | u8 version | u16 record_bytes | u32 record_count | u32 payload_bytes
inline constexpr uint16_t SECTION_HEADER_BYTES = 12;

enum SectionType : uint8_t {
    SECTION_ACCEL_RAW = 1,
    SECTION_GPS_FIXES = 2,
    SECTION_BOARD_TEMP = 3,
    SECTION_COW_TEMP = 4,
    SECTION_MICROPHONE_RAW = 5,
    SECTION_DIAGNOSTICS = 6,
};

inline constexpr uint8_t SECTION_VERSION_1 = 1;

// ACCEL_RAW record (6): i16 x_raw | i16 y_raw | i16 z_raw
inline constexpr uint16_t ACCEL_RECORD_BYTES = 6;
// GPS record (24): u32 capture_offset_ms | i32 lat_e7 | i32 lon_e7 | f32 speed_mps | f32 course_deg |
//                  u8 fix_quality | u8 satellites | u8 flags(bit0 valid, bit1 utc_valid) | u8 reserved
inline constexpr uint16_t GPS_RECORD_BYTES = 24;
// BOARD_TEMP record (20): u32 timestamp_ms | i32 adc_raw | i32 millivolts | f32 temperature_c |
//                         u8 status | u8 flags(bit0 millivolts_valid) | u16 reserved
inline constexpr uint16_t BOARD_TEMP_RECORD_BYTES = 20;
// COW_TEMP record (24): u32 timestamp_ms | i32 adc_raw | i32 millivolts | f32 resistance_ohm |
//                       f32 temperature_c | u8 status | u8 flags(bit0 millivolts_valid) | u16 reserved
inline constexpr uint16_t COW_TEMP_RECORD_BYTES = 24;
// MICROPHONE_RAW record (2): u16 raw ADC code (uncentered, unscaled)
inline constexpr uint16_t MIC_RECORD_BYTES = 2;
// DIAGNOSTICS: one record, see capture_stream_encoder.cpp encode_diagnostics() for the field list.
inline constexpr uint16_t DIAGNOSTICS_RECORD_BYTES = 144;

inline constexpr uint8_t GPS_FLAG_VALID = 0x01;
inline constexpr uint8_t GPS_FLAG_UTC_VALID = 0x02;
inline constexpr uint8_t TEMP_FLAG_MV_VALID = 0x01;

}  // namespace cownect::codec::stream
