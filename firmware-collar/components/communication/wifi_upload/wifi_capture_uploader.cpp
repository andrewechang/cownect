#include "wifi_capture_uploader.h"

#include "byte_codec.h"
#include "cownect_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "prototype_tcp_client.h"
#include "wifi_manager.h"

namespace cownect::upload {
namespace {
constexpr const char* TAG = "UPLOAD";
uint8_t* s_chunk = nullptr;  // allocated once, reused every cycle
size_t s_chunk_bytes = 0;

uint32_t ms_since(int64_t t0)
{
    return static_cast<uint32_t>((esp_timer_get_time() - t0) / 1000);
}
}  // namespace

WifiCaptureUploader& wifi_uploader()
{
    static WifiCaptureUploader instance;
    return instance;
}

size_t encode_upload_header(uint64_t device_id, uint32_t capture_id, uint32_t stream_bytes, uint8_t* dst,
                            size_t capacity)
{
    codec::ByteWriter w(dst, capacity);
    w.u32(UPLOAD_MAGIC);
    w.u8(UPLOAD_VERSION);
    w.u64(device_id);
    w.u32(capture_id);
    w.u32(stream_bytes);
    return w.overflow() ? 0 : w.position();
}

esp_err_t WifiCaptureUploader::upload_capture(const data::CaptureSession& capture, WifiUploadStats& stats)
{
    stats = {};
    stats.capture_id = capture.id.capture_id;
    esp_err_t err = encoder_.prepare(capture);
    if (err != ESP_OK) {
        stats.error = err;
        stats.failure_stage = "ENCODER";
        return err;
    }
    return upload_stream(encoder_, capture.id.device_id, capture.id.capture_id, stats);
}

esp_err_t WifiCaptureUploader::upload_stream(const codec::IByteStream& stream, uint64_t device_id,
                                             uint32_t capture_id, WifiUploadStats& stats)
{
    stats = {};
    stats.capture_id = capture_id;
    stats.stream_bytes = stream.total_bytes();
    stats.failure_stage = "";

    wifi::PrototypeWifiConfig cfg;
    const char* missing = nullptr;
    esp_err_t err = wifi::prototype_wifi_config_load(cfg, &missing);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "not attempted: CONFIG_NOT_SET (%s)", missing);
        stats.error = err;
        stats.failure_stage = "CONFIG_NOT_SET";
        return err;
    }
    if (s_chunk == nullptr || s_chunk_bytes != cfg.upload_chunk_bytes) {
        heap_caps_free(s_chunk);
        s_chunk = static_cast<uint8_t*>(heap_caps_malloc(cfg.upload_chunk_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        s_chunk_bytes = s_chunk ? cfg.upload_chunk_bytes : 0;
        if (s_chunk == nullptr) {
            stats.error = ESP_ERR_NO_MEM;
            stats.failure_stage = "ENCODER";
            return ESP_ERR_NO_MEM;
        }
    }

    wifi::WifiManager& wm = wifi::wifi_manager();
    tcp::PrototypeTcpClient tcp;
    int64_t t0 = esp_timer_get_time();
    err = wm.connect(cfg);
    stats.wifi_connect_ms = ms_since(t0);
    if (err == ESP_OK) {
        stats.wifi_connected = true;
        t0 = esp_timer_get_time();
        err = tcp.connect(cfg.jetson_ip, cfg.jetson_port, cfg.tcp_connect_timeout_ms);
        stats.tcp_connect_ms = ms_since(t0);
        if (err == ESP_OK) {
            stats.tcp_connected = true;
            t0 = esp_timer_get_time();
            uint8_t header[UPLOAD_HEADER_BYTES];
            encode_upload_header(device_id, capture_id, stats.stream_bytes, header, sizeof(header));
            err = tcp.send_all(header, sizeof(header), cfg.socket_write_timeout_ms);
            if (err != ESP_OK) {
                stats.failure_stage = "HEADER";
            }
            ESP_LOGI(TAG, "capture=%u bytes=%u", static_cast<unsigned>(capture_id), static_cast<unsigned>(stats.stream_bytes));
            uint32_t offset = 0;
            while (err == ESP_OK && offset < stats.stream_bytes) {
                size_t n = 0;
                err = stream.read(offset, s_chunk, s_chunk_bytes, n);
                if (err != ESP_OK || n == 0) {
                    err = err != ESP_OK ? err : ESP_FAIL;
                    stats.failure_stage = "ENCODER";
                    break;
                }
                const uint64_t before = tcp.bytes_sent();
                err = tcp.send_all(s_chunk, n, cfg.socket_write_timeout_ms);
                offset += static_cast<uint32_t>(tcp.bytes_sent() - before);
                if (err != ESP_OK) {
                    stats.failure_stage = "STREAM";
                }
            }
            stats.bytes_sent = offset;
            stats.upload_ms = ms_since(t0);
        } else {
            stats.failure_stage = "TCP_CONNECT";
        }
        stats.socket_write_calls = tcp.write_calls();
        stats.socket_errors = tcp.socket_errors();
        tcp.close();
    } else {
        stats.failure_stage = "WIFI";
    }
    // Wi-Fi is stopped before returning so the scheduler can sleep (Material 14 section 21).
    wm.disconnect();
    wm.stop();

    stats.upload_complete = err == ESP_OK && stats.tcp_connected && stats.bytes_sent == stats.stream_bytes;
    stats.error = err;
    if (stats.upload_complete) {
        ESP_LOGI(TAG, "complete sent=%u upload_ms=%u", static_cast<unsigned>(stats.bytes_sent),
                 static_cast<unsigned>(stats.upload_ms));
        return ESP_OK;
    }
    ESP_LOGW(TAG, "FAILED stage=%s sent=%u/%u (%s)", stats.failure_stage, static_cast<unsigned>(stats.bytes_sent),
             static_cast<unsigned>(stats.stream_bytes), cownect_err_name(err));
    return err != ESP_OK ? err : COWNECT_ERR_INCOMPLETE;
}

}  // namespace cownect::upload
