#pragma once

#include <cstdint>
#include "byte_stream.h"
#include "capture_stream_encoder.h"
#include "capture_types.h"
#include "esp_err.h"

namespace cownect::upload {

// Prototype upload header, 21 bytes, little-endian (Material 14 Rev B section 13):
//   u32 magic (0x50554E43, bytes "CNUP") | u8 version (1) | u64 device_id | u32 capture_id |
//   u32 stream_bytes
// followed by exactly stream_bytes of the Material 13 capture stream.
inline constexpr uint32_t UPLOAD_MAGIC = 0x50554E43u;
inline constexpr uint8_t UPLOAD_VERSION = 1;
inline constexpr size_t UPLOAD_HEADER_BYTES = 21;

size_t encode_upload_header(uint64_t device_id, uint32_t capture_id, uint32_t stream_bytes, uint8_t* dst,
                            size_t capacity);

// Material 14 section 16.
struct WifiUploadStats {
    uint32_t capture_id;

    uint32_t stream_bytes;
    uint32_t bytes_sent;  // stream bytes accepted by the socket (header excluded)

    uint32_t wifi_connect_ms;
    uint32_t tcp_connect_ms;
    uint32_t upload_ms;

    uint32_t socket_write_calls;
    uint32_t socket_errors;

    bool wifi_connected;
    bool tcp_connected;
    bool upload_complete;

    esp_err_t error;
    const char* failure_stage;  // "", "CONFIG_NOT_SET", "WIFI", "TCP_CONNECT", "HEADER", "STREAM", "ENCODER"
};

// CaptureSession -> CaptureStreamEncoder -> small chunk -> TCP -> Jetson. One reusable chunk
// buffer; no second full-capture buffer. Always stops Wi-Fi before returning.
class WifiCaptureUploader {
public:
    esp_err_t upload_capture(const data::CaptureSession& capture, WifiUploadStats& stats);
    esp_err_t upload_stream(const codec::IByteStream& stream, uint64_t device_id, uint32_t capture_id,
                            WifiUploadStats& stats);

private:
    codec::CaptureStreamEncoder encoder_;
};

WifiCaptureUploader& wifi_uploader();

}  // namespace cownect::upload
