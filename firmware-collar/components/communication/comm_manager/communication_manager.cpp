#include "communication_manager.h"

#include "capture_session.h"
#include "cownect_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lora_test_packet.h"
#include "prototype_telemetry_encoder.h"
#include "sdkconfig.h"
#include "sx1262_adapter.h"

namespace cownect::comm {
namespace {
constexpr const char* TAG = "COMM";
}

const char* communication_mode_name(CommunicationMode m)
{
    return m == CommunicationMode::LORA_FULL ? "LORA_FULL" : "HYBRID_LORA_WIFI";
}

bool communication_mode_from_config(CommunicationMode& out)
{
#if CONFIG_COWNECT_COMM_MODE_LORA_FULL
    out = CommunicationMode::LORA_FULL;
    return true;
#elif CONFIG_COWNECT_COMM_MODE_HYBRID
    out = CommunicationMode::HYBRID_LORA_WIFI;
    return true;
#else
    (void)out;
    return false;
#endif
}

CommunicationManager& communication_manager()
{
    static CommunicationManager instance;
    return instance;
}

lorafull::LoraFullTransfer& lora_full_transfer()
{
    static lorafull::LoraFullTransfer instance(radio::lora_radio());
    return instance;
}

const lorafull::LoraFullTransferStats& CommunicationManager::last_lora_full_stats() const
{
    return lora_full_transfer().stats();
}

esp_err_t CommunicationManager::init(CommunicationMode mode)
{
    mode_ = mode;
    ESP_LOGI(TAG, "mode=%s", communication_mode_name(mode));
    return ESP_OK;
}

esp_err_t CommunicationManager::send_test_packet(uint64_t device_id, uint32_t sequence)
{
    esp_err_t err = radio::lora_radio_prepare_from_config();
    if (err != ESP_OK) {
        return err;
    }
    radio::LoraTestPacket p = {radio::LORA_TEST_VERSION, radio::LoraTestType::PING, device_id, sequence, 0};
    uint8_t buf[radio::LORA_TEST_PACKET_BYTES];
    const size_t n = radio::lora_test_packet_encode(p, buf, sizeof(buf));
    uint32_t ms = 0;
    return radio::lora_radio().send_blocking(buf, n, ms);
}

esp_err_t CommunicationManager::send_telemetry_once(const features::TelemetryRecord& record)
{
    uint8_t buf[hybrid::TELEMETRY_PACKET_BYTES];
    size_t n = 0;
    esp_err_t err = hybrid::PrototypeTelemetryEncoder().encode(record, buf, sizeof(buf), n);
    if (err == ESP_OK) err = radio::lora_radio_prepare_from_config();
    uint32_t ms = 0;
    if (err == ESP_OK) err = radio::lora_radio().send_blocking(buf, n, ms);
    return err;
}

esp_err_t CommunicationManager::send_capture_full(const data::CaptureSession& capture, CommunicationMetrics& m)
{
    const int64_t t0 = esp_timer_get_time();
    m.mode = CommunicationMode::LORA_FULL;
    esp_err_t err = radio::lora_radio_prepare_from_config();
    lorafull::LoraFullTransfer& xfer = lora_full_transfer();
    if (err == ESP_OK) err = xfer.begin(capture);
    if (err == ESP_OK) {
        m.attempted = true;
        // No cut-off at the 120 s boundary: runs until complete or bounded failure policy.
        err = xfer.run_to_completion();
    }
    const auto& s = xfer.stats();
    m.raw_capture_bytes = s.stream_bytes;
    m.lora_payload_bytes = s.stream_bytes;
    m.lora_packets_attempted = s.fragments_tx_started;
    m.lora_packets_sent = s.fragments_tx_done;
    m.lora_retries = s.retry_count;
    m.lora_transfer_time_ms = s.communication_time_ms;
    m.success = err == ESP_OK && s.transfer_complete_sender_side;
    m.result = err;
    m.communication_time_ms = static_cast<uint32_t>((esp_timer_get_time() - t0) / 1000);
    return err;
}

esp_err_t CommunicationManager::submit_capture(data::CaptureSession& capture,
                                               const features::TelemetryRecord& telemetry, CommunicationMetrics& m,
                                               hybrid::SwitchedRailRelease release_rail, void* release_ctx)
{
    m = {};
    m.implemented = true;
    m.mode = mode_;
    const int64_t t0 = esp_timer_get_time();
    data::capture_session_freeze(capture);  // buffers owned by the transfer
    esp_err_t err;
    if (mode_ == CommunicationMode::LORA_FULL) {
        err = send_capture_full(capture, m);
    } else {
        m.attempted = true;
        err = hybrid_.run(capture, telemetry, release_rail, release_ctx);
        const auto& h = hybrid_.stats();
        m.raw_capture_bytes = h.wifi.stream_bytes;
        m.lora_payload_bytes = h.lora_bytes;
        m.lora_packets_attempted = h.lora_attempted ? 1 : 0;
        m.lora_packets_sent = h.lora_success ? 1 : 0;
        m.lora_transfer_time_ms = h.lora_time_ms;
        m.wifi_payload_bytes = h.wifi_bytes;
        m.wifi_connect_time_ms = h.wifi.wifi_connect_ms;
        m.wifi_transfer_time_ms = h.wifi.upload_ms;
        m.wifi_transfer_success = h.wifi_success;
        m.success = h.result == hybrid::HybridResult::BOTH_OK;
        m.result = err;
    }
    data::capture_session_release(capture);
    m.communication_time_ms = static_cast<uint32_t>((esp_timer_get_time() - t0) / 1000);
    ESP_LOGI(TAG, "mode=%s success=%d comm_ms=%u", communication_mode_name(mode_), m.success,
             static_cast<unsigned>(m.communication_time_ms));
    return err;
}

}  // namespace cownect::comm
