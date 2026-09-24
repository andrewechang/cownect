#pragma once

#include <cstddef>
#include <cstdint>
#include "capture_types.h"
#include "esp_err.h"

// Reference decoder for the capture stream (used by unit tests; mirrors the Jetson decoder).
namespace cownect::codec {

struct ParsedSection {
    uint8_t type;
    uint8_t version;
    uint16_t record_bytes;
    uint32_t record_count;
    uint32_t payload_bytes;
    size_t payload_offset;
};

struct ParsedCaptureStream {
    uint32_t magic;
    uint8_t stream_version;
    uint16_t header_bytes;
    uint64_t device_id;
    uint32_t capture_id;
    uint64_t capture_start_us;
    uint64_t capture_end_us;
    uint32_t total_stream_bytes;
    uint32_t accel_count;
    uint32_t gps_count;
    uint32_t board_temp_count;
    uint32_t cow_temp_count;
    uint32_t microphone_count;
    uint32_t microphone_sample_rate_hz;
    uint32_t capture_status_flags;

    ParsedSection sections[8];
    size_t section_count;
};

// Validates magic/version/sizes and locates sections (unknown section types are skipped).
esp_err_t capture_stream_parse(const uint8_t* buf, size_t len, ParsedCaptureStream& out);
const ParsedSection* capture_stream_find(const ParsedCaptureStream& p, uint8_t type);

data::AccelSample decode_accel_record(const uint8_t* rec);
data::GpsFix decode_gps_record(const uint8_t* rec);
data::BoardTemperatureSample decode_board_temp_record(const uint8_t* rec);
data::CowTemperatureSample decode_cow_temp_record(const uint8_t* rec);

}  // namespace cownect::codec
