#include "capture_stream_encoder.h"

#include <algorithm>
#include "board_adc.h"
#include "byte_codec.h"
#include "capture_session.h"
#include "crc32.h"

namespace cownect::codec {

using namespace stream;

esp_err_t stream_crc32(const IByteStream& s, uint32_t& crc_out)
{
    uint8_t buf[256];
    uint32_t state = crc32_init();
    uint32_t off = 0;
    const uint32_t total = s.total_bytes();
    while (off < total) {
        size_t n = 0;
        esp_err_t err = s.read(off, buf, sizeof(buf), n);
        if (err != ESP_OK || n == 0) {
            return err != ESP_OK ? err : ESP_FAIL;
        }
        state = crc32_update(state, buf, n);
        off += static_cast<uint32_t>(n);
    }
    crc_out = crc32_final(state);
    return ESP_OK;
}

bool CaptureStreamEncoder::add_segment(Kind kind, uint64_t length, uint16_t record_bytes, uint32_t blob_offset,
                                       uint64_t& cursor)
{
    if (seg_count_ >= sizeof(segs_) / sizeof(segs_[0]) || cursor + length > UINT32_MAX) {
        return false;
    }
    if (length > 0) {
        segs_[seg_count_++] = {static_cast<uint32_t>(cursor), static_cast<uint32_t>(length), kind, record_bytes,
                               blob_offset};
    }
    cursor += length;
    return true;
}

static void write_section_header(uint8_t* out, SectionType type, uint16_t record_bytes, uint32_t count)
{
    ByteWriter w(out, SECTION_HEADER_BYTES);
    w.u8(type);
    w.u8(SECTION_VERSION_1);
    w.u16(record_bytes);
    w.u32(count);
    w.u32(static_cast<uint32_t>(static_cast<uint64_t>(record_bytes) * count));
}

void CaptureStreamEncoder::encode_diagnostics(uint8_t* out) const
{
    const data::CaptureDiagnostics& d = cap_->diagnostics;
    ByteWriter w(out, DIAGNOSTICS_RECORD_BYTES);
    w.u32(d.accelerometer.fifo_overrun_count);
    w.u32(d.accelerometer.i2c_error_count);
    w.u32(d.accelerometer.dropped_samples);
    w.u32(d.accelerometer.discarded_startup_samples);
    w.u32(d.gps.uart_bytes_received);
    w.u32(d.gps.uart_overflow_count);
    w.u32(d.gps.checksum_error_count);
    w.u32(d.gps.parse_error_count);
    w.u32(d.gps.dropped_fix_count);
    w.u32(d.gps.valid_fix_count);
    w.u32(d.gps.framed_sentence_count);
    w.u32(d.adc.pool_overflow_count);
    w.u32(d.adc.driver_error_count);
    w.u32(d.adc.read_timeout_count);
    w.u32(d.adc.unexpected_channel_results);
    w.u32(d.adc.pattern_order_errors);
    w.u32(d.adc.invalid_results);
    w.u32(d.microphone.destination_overflow_count);
    w.u32(d.microphone.low_rail_count);
    w.u32(d.microphone.high_rail_count);
    w.u32(d.temperature.board_adc_errors);
    w.u32(d.temperature.board_conversion_errors);
    w.u32(d.temperature.cow_adc_errors);
    w.u32(d.temperature.cow_conversion_errors);
    w.u32(d.temperature.cow_open_suspected);
    w.u32(d.temperature.cow_short_suspected);
    w.u32(d.sensor_manager_service_overrun_count);
    w.u64(d.requested_capture_us);
    w.u64(d.actual_capture_us);
    w.f32(static_cast<float>(d.microphone.raw_mean));
    w.f32(static_cast<float>(d.microphone.raw_rms_ac));
    w.u16(d.microphone.raw_min);
    w.u16(d.microphone.raw_max);
    w.u8(static_cast<uint8_t>(board::adc_attenuation()));  // adc_atten_t numeric code
    w.u8(12);                                              // ADC bit width
    w.u8(d.adc.pattern_len);
    w.u8(0);
    w.u32(d.adc.sample_freq_hz);
}

esp_err_t CaptureStreamEncoder::prepare(const data::CaptureSession& c)
{
    cap_ = nullptr;
    total_ = 0;
    seg_count_ = 0;
    if (c.state != data::CaptureState::COMPLETE && c.state != data::CaptureState::FROZEN_FOR_TRANSFER) {
        return ESP_ERR_INVALID_STATE;
    }
    if ((c.accel_count && !c.accel_samples) || (c.gps_fix_count && !c.gps_fixes) ||
        (c.board_temperature_count && !c.board_temperature_samples) ||
        (c.cow_temperature_count && !c.cow_temperature_samples) ||
        (c.microphone_sample_count && !c.microphone_raw_samples)) {
        return ESP_ERR_INVALID_ARG;
    }
    cap_ = &c;

    // Blob layout: [header][6 section headers][diagnostics payload]
    const uint32_t sh = HEADER_BYTES;
    const uint32_t diag_off = HEADER_BYTES + 6 * SECTION_HEADER_BYTES;
    uint64_t cursor = 0;
    bool ok = add_segment(Kind::BLOB, HEADER_BYTES, 0, 0, cursor);
    ok = ok && add_segment(Kind::BLOB, SECTION_HEADER_BYTES, 0, sh + 0 * SECTION_HEADER_BYTES, cursor);
    ok = ok && add_segment(Kind::ACCEL, static_cast<uint64_t>(c.accel_count) * ACCEL_RECORD_BYTES, ACCEL_RECORD_BYTES, 0, cursor);
    ok = ok && add_segment(Kind::BLOB, SECTION_HEADER_BYTES, 0, sh + 1 * SECTION_HEADER_BYTES, cursor);
    ok = ok && add_segment(Kind::GPS, static_cast<uint64_t>(c.gps_fix_count) * GPS_RECORD_BYTES, GPS_RECORD_BYTES, 0, cursor);
    ok = ok && add_segment(Kind::BLOB, SECTION_HEADER_BYTES, 0, sh + 2 * SECTION_HEADER_BYTES, cursor);
    ok = ok && add_segment(Kind::BOARD, static_cast<uint64_t>(c.board_temperature_count) * BOARD_TEMP_RECORD_BYTES,
                           BOARD_TEMP_RECORD_BYTES, 0, cursor);
    ok = ok && add_segment(Kind::BLOB, SECTION_HEADER_BYTES, 0, sh + 3 * SECTION_HEADER_BYTES, cursor);
    ok = ok && add_segment(Kind::COW, static_cast<uint64_t>(c.cow_temperature_count) * COW_TEMP_RECORD_BYTES,
                           COW_TEMP_RECORD_BYTES, 0, cursor);
    ok = ok && add_segment(Kind::BLOB, SECTION_HEADER_BYTES, 0, sh + 4 * SECTION_HEADER_BYTES, cursor);
    ok = ok && add_segment(Kind::MIC, static_cast<uint64_t>(c.microphone_sample_count) * MIC_RECORD_BYTES,
                           MIC_RECORD_BYTES, 0, cursor);
    ok = ok && add_segment(Kind::BLOB, SECTION_HEADER_BYTES, 0, sh + 5 * SECTION_HEADER_BYTES, cursor);
    ok = ok && add_segment(Kind::BLOB, DIAGNOSTICS_RECORD_BYTES, 0, diag_off, cursor);
    if (!ok) {
        cap_ = nullptr;
        return ESP_ERR_INVALID_SIZE;  // 32-bit stream offset overflow
    }
    total_ = static_cast<uint32_t>(cursor);

    ByteWriter h(blob_, HEADER_BYTES);
    h.u32(CAPTURE_STREAM_MAGIC);
    h.u8(CAPTURE_STREAM_VERSION);
    h.u8(0);
    h.u16(HEADER_BYTES);
    h.u64(c.id.device_id);
    h.u32(c.id.capture_id);
    h.u64(c.capture_start_us);
    h.u64(c.capture_end_us);
    h.u32(total_);
    h.u32(static_cast<uint32_t>(c.accel_count));
    h.u32(static_cast<uint32_t>(c.gps_fix_count));
    h.u32(static_cast<uint32_t>(c.board_temperature_count));
    h.u32(static_cast<uint32_t>(c.cow_temperature_count));
    h.u32(static_cast<uint32_t>(c.microphone_sample_count));
    h.u32(c.microphone_sample_rate_hz);
    h.u32(data::capture_status_bits(c.status));

    write_section_header(&blob_[sh + 0 * SECTION_HEADER_BYTES], SECTION_ACCEL_RAW, ACCEL_RECORD_BYTES, c.accel_count);
    write_section_header(&blob_[sh + 1 * SECTION_HEADER_BYTES], SECTION_GPS_FIXES, GPS_RECORD_BYTES, c.gps_fix_count);
    write_section_header(&blob_[sh + 2 * SECTION_HEADER_BYTES], SECTION_BOARD_TEMP, BOARD_TEMP_RECORD_BYTES,
                         c.board_temperature_count);
    write_section_header(&blob_[sh + 3 * SECTION_HEADER_BYTES], SECTION_COW_TEMP, COW_TEMP_RECORD_BYTES,
                         c.cow_temperature_count);
    write_section_header(&blob_[sh + 4 * SECTION_HEADER_BYTES], SECTION_MICROPHONE_RAW, MIC_RECORD_BYTES,
                         c.microphone_sample_count);
    write_section_header(&blob_[sh + 5 * SECTION_HEADER_BYTES], SECTION_DIAGNOSTICS, DIAGNOSTICS_RECORD_BYTES, 1);
    encode_diagnostics(&blob_[diag_off]);
    return ESP_OK;
}

size_t CaptureStreamEncoder::encode_record(Kind kind, uint32_t i, uint8_t* out) const
{
    ByteWriter w(out, 32);
    switch (kind) {
    case Kind::ACCEL: {
        const auto& s = cap_->accel_samples[i];
        w.i16(s.x_raw);
        w.i16(s.y_raw);
        w.i16(s.z_raw);
        break;
    }
    case Kind::GPS: {
        const auto& f = cap_->gps_fixes[i];
        w.u32(f.capture_offset_ms);
        w.i32(f.latitude_e7);
        w.i32(f.longitude_e7);
        w.f32(f.speed_mps);
        w.f32(f.course_deg);
        w.u8(f.fix_quality);
        w.u8(f.satellites);
        w.u8(static_cast<uint8_t>((f.valid ? GPS_FLAG_VALID : 0) | (f.utc_valid ? GPS_FLAG_UTC_VALID : 0)));
        w.u8(0);
        break;
    }
    case Kind::BOARD: {
        const auto& s = cap_->board_temperature_samples[i];
        w.u32(s.timestamp_ms);
        w.i32(s.adc_raw);
        w.i32(s.millivolts);
        w.f32(s.temperature_c);
        w.u8(static_cast<uint8_t>(s.status));
        w.u8(s.millivolts_valid ? TEMP_FLAG_MV_VALID : 0);
        w.u16(0);
        break;
    }
    case Kind::COW: {
        const auto& s = cap_->cow_temperature_samples[i];
        w.u32(s.timestamp_ms);
        w.i32(s.adc_raw);
        w.i32(s.millivolts);
        w.f32(s.resistance_ohm);
        w.f32(s.temperature_c);
        w.u8(static_cast<uint8_t>(s.status));
        w.u8(s.millivolts_valid ? TEMP_FLAG_MV_VALID : 0);
        w.u16(0);
        break;
    }
    case Kind::MIC:
        w.u16(cap_->microphone_raw_samples[i]);
        break;
    case Kind::BLOB:
        break;
    }
    return w.position();
}

esp_err_t CaptureStreamEncoder::read(uint32_t offset, uint8_t* dst, size_t cap, size_t& written) const
{
    written = 0;
    if (cap_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    if (offset > total_ || dst == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t seg = 0;
    while (written < cap && offset < total_) {
        while (seg < seg_count_ && offset >= segs_[seg].start + segs_[seg].length) {
            ++seg;
        }
        if (seg >= seg_count_) {
            return ESP_FAIL;  // layout inconsistency (should be impossible)
        }
        const Segment& s = segs_[seg];
        const uint32_t rel = offset - s.start;
        size_t n = 0;
        if (s.kind == Kind::BLOB) {
            n = std::min<size_t>(cap - written, s.length - rel);
            std::copy_n(&blob_[s.blob_offset + rel], n, dst + written);
        } else {
            uint8_t rec[32];
            const uint32_t index = rel / s.record_bytes;
            const uint32_t inner = rel % s.record_bytes;
            encode_record(s.kind, index, rec);
            n = std::min<size_t>(cap - written, s.record_bytes - inner);
            std::copy_n(&rec[inner], n, dst + written);
        }
        written += n;
        offset += static_cast<uint32_t>(n);
    }
    return ESP_OK;
}

}  // namespace cownect::codec
