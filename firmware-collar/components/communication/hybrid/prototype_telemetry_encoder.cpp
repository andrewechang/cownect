#include "prototype_telemetry_encoder.h"

#include "byte_codec.h"

namespace cownect::hybrid {

esp_err_t PrototypeTelemetryEncoder::encode(const features::TelemetryRecord& r, uint8_t* dst, size_t cap,
                                            size_t& written) const
{
    written = 0;
    if (dst == nullptr || cap < TELEMETRY_PACKET_BYTES) {
        return ESP_ERR_INVALID_SIZE;
    }
    uint8_t valid = 0;
    if (r.gps_valid) valid |= TEL_VALID_GPS;
    if (r.board_temp_valid) valid |= TEL_VALID_BOARD_TEMP;
    if (r.cow_temp_valid) valid |= TEL_VALID_COW_TEMP;
    if (r.accel_valid) valid |= TEL_VALID_ACCEL;
    if (r.microphone_valid) valid |= TEL_VALID_MIC;

    codec::ByteWriter w(dst, cap);
    w.u16(TELEMETRY_MAGIC);
    w.u8(TELEMETRY_PACKET_VERSION);
    w.u8(TELEMETRY_PACKET_TYPE);
    w.u64(r.device_id);
    w.u32(r.capture_id);
    w.u8(valid);
    w.i32(r.gps_valid ? r.latitude_e7 : 0);
    w.i32(r.gps_valid ? r.longitude_e7 : 0);
    w.f32(r.board_temp_valid ? r.board_temp_c : 0.0f);
    w.f32(r.cow_temp_valid ? r.cow_temp_c : 0.0f);
    w.f32(r.accel_valid ? r.accel_magnitude_mean_g : 0.0f);
    w.f32(r.accel_valid ? r.accel_magnitude_rms_g : 0.0f);
    w.f32(r.microphone_valid ? r.microphone_rms_ac_raw : 0.0f);
    w.u32(r.status_flags);
    if (w.overflow() || w.position() != TELEMETRY_PACKET_BYTES) {
        return ESP_FAIL;
    }
    written = w.position();
    return ESP_OK;
}

esp_err_t prototype_telemetry_decode(const uint8_t* src, size_t length, features::TelemetryRecord& out)
{
    out = {};
    if (src == nullptr || length != TELEMETRY_PACKET_BYTES) {
        return ESP_ERR_INVALID_SIZE;
    }
    codec::ByteReader r(src, length);
    if (r.u16() != TELEMETRY_MAGIC) return ESP_ERR_INVALID_RESPONSE;
    out.protocol_version = r.u8();
    if (out.protocol_version != TELEMETRY_PACKET_VERSION || r.u8() != TELEMETRY_PACKET_TYPE) {
        return ESP_ERR_INVALID_VERSION;
    }
    out.device_id = r.u64();
    out.capture_id = r.u32();
    const uint8_t valid = r.u8();
    out.gps_valid = valid & TEL_VALID_GPS;
    out.board_temp_valid = valid & TEL_VALID_BOARD_TEMP;
    out.cow_temp_valid = valid & TEL_VALID_COW_TEMP;
    out.accel_valid = valid & TEL_VALID_ACCEL;
    out.microphone_valid = valid & TEL_VALID_MIC;
    out.latitude_e7 = r.i32();
    out.longitude_e7 = r.i32();
    out.board_temp_c = r.f32();
    out.cow_temp_c = r.f32();
    out.accel_magnitude_mean_g = r.f32();
    out.accel_magnitude_rms_g = r.f32();
    out.microphone_rms_ac_raw = r.f32();
    out.status_flags = r.u32();
    return r.error() ? ESP_ERR_INVALID_SIZE : ESP_OK;
}

}  // namespace cownect::hybrid
