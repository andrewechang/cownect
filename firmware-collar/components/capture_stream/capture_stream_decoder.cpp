#include "capture_stream_decoder.h"

#include "byte_codec.h"
#include "capture_stream_format.h"

namespace cownect::codec {

using namespace stream;

esp_err_t capture_stream_parse(const uint8_t* buf, size_t len, ParsedCaptureStream& p)
{
    p = {};
    if (buf == nullptr || len < HEADER_BYTES) {
        return ESP_ERR_INVALID_SIZE;
    }
    ByteReader r(buf, len);
    p.magic = r.u32();
    p.stream_version = r.u8();
    r.u8();
    p.header_bytes = r.u16();
    p.device_id = r.u64();
    p.capture_id = r.u32();
    p.capture_start_us = r.u64();
    p.capture_end_us = r.u64();
    p.total_stream_bytes = r.u32();
    p.accel_count = r.u32();
    p.gps_count = r.u32();
    p.board_temp_count = r.u32();
    p.cow_temp_count = r.u32();
    p.microphone_count = r.u32();
    p.microphone_sample_rate_hz = r.u32();
    p.capture_status_flags = r.u32();
    if (r.error() || p.magic != CAPTURE_STREAM_MAGIC || p.stream_version != CAPTURE_STREAM_VERSION ||
        p.header_bytes < HEADER_BYTES || p.total_stream_bytes != len) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    size_t pos = p.header_bytes;
    while (pos < len) {
        if (len - pos < SECTION_HEADER_BYTES) {
            return ESP_ERR_INVALID_SIZE;
        }
        ByteReader s(buf + pos, SECTION_HEADER_BYTES);
        ParsedSection sec = {};
        sec.type = s.u8();
        sec.version = s.u8();
        sec.record_bytes = s.u16();
        sec.record_count = s.u32();
        sec.payload_bytes = s.u32();
        sec.payload_offset = pos + SECTION_HEADER_BYTES;
        if (static_cast<uint64_t>(sec.record_bytes) * sec.record_count != sec.payload_bytes ||
            sec.payload_offset + sec.payload_bytes > len) {
            return ESP_ERR_INVALID_SIZE;
        }
        if (p.section_count < sizeof(p.sections) / sizeof(p.sections[0])) {
            p.sections[p.section_count++] = sec;
        }
        pos = sec.payload_offset + sec.payload_bytes;
    }
    return ESP_OK;
}

const ParsedSection* capture_stream_find(const ParsedCaptureStream& p, uint8_t type)
{
    for (size_t i = 0; i < p.section_count; ++i) {
        if (p.sections[i].type == type) return &p.sections[i];
    }
    return nullptr;
}

data::AccelSample decode_accel_record(const uint8_t* rec)
{
    ByteReader r(rec, ACCEL_RECORD_BYTES);
    data::AccelSample s;
    s.x_raw = r.i16();
    s.y_raw = r.i16();
    s.z_raw = r.i16();
    return s;
}

data::GpsFix decode_gps_record(const uint8_t* rec)
{
    ByteReader r(rec, GPS_RECORD_BYTES);
    data::GpsFix f = {};
    f.capture_offset_ms = r.u32();
    f.latitude_e7 = r.i32();
    f.longitude_e7 = r.i32();
    f.speed_mps = r.f32();
    f.course_deg = r.f32();
    f.fix_quality = r.u8();
    f.satellites = r.u8();
    const uint8_t flags = r.u8();
    f.valid = (flags & GPS_FLAG_VALID) != 0;
    f.utc_valid = (flags & GPS_FLAG_UTC_VALID) != 0;
    return f;
}

data::BoardTemperatureSample decode_board_temp_record(const uint8_t* rec)
{
    ByteReader r(rec, BOARD_TEMP_RECORD_BYTES);
    data::BoardTemperatureSample s = {};
    s.timestamp_ms = r.u32();
    s.adc_raw = r.i32();
    s.millivolts = r.i32();
    s.temperature_c = r.f32();
    s.status = static_cast<sensors::TemperatureStatus>(r.u8());
    s.millivolts_valid = (r.u8() & TEMP_FLAG_MV_VALID) != 0;
    return s;
}

data::CowTemperatureSample decode_cow_temp_record(const uint8_t* rec)
{
    ByteReader r(rec, COW_TEMP_RECORD_BYTES);
    data::CowTemperatureSample s = {};
    s.timestamp_ms = r.u32();
    s.adc_raw = r.i32();
    s.millivolts = r.i32();
    s.resistance_ohm = r.f32();
    s.temperature_c = r.f32();
    s.status = static_cast<sensors::TemperatureStatus>(r.u8());
    s.millivolts_valid = (r.u8() & TEMP_FLAG_MV_VALID) != 0;
    return s;
}

}  // namespace cownect::codec
