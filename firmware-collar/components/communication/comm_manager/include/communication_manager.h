#pragma once

#include <cstdint>
#include "capture_types.h"
#include "esp_err.h"
#include "hybrid_communication_manager.h"
#include "lora_full_transfer.h"
#include "telemetry_record.h"

namespace cownect::comm {

// Material 3 section 16 / Material 4 section 15. Selected here, never read by sensor drivers.
enum class CommunicationMode { LORA_FULL, HYBRID_LORA_WIFI };
const char* communication_mode_name(CommunicationMode m);

// Returns false when communication is disabled in menuconfig (the default until RF/network
// values are configured).
bool communication_mode_from_config(CommunicationMode& out);

// Strategy comparison metrics (Material 3 section 25).
struct CommunicationMetrics {
    bool implemented;  // false: nothing attempted (disabled / CONFIG_NOT_SET)
    bool attempted;
    bool success;
    esp_err_t result;
    CommunicationMode mode;

    uint32_t raw_capture_bytes;
    uint32_t lora_payload_bytes;
    uint32_t wifi_payload_bytes;

    uint32_t lora_packets_attempted;
    uint32_t lora_packets_sent;
    uint32_t lora_retries;
    uint32_t lora_transfer_time_ms;

    uint32_t wifi_connect_time_ms;
    uint32_t wifi_transfer_time_ms;
    bool wifi_transfer_success;

    uint32_t communication_time_ms;
};

class CommunicationManager {
public:
    esp_err_t init(CommunicationMode mode);
    CommunicationMode mode() const { return mode_; }

    // Dispatches the selected strategy. Freezes the capture for the duration of the transfer
    // (no reuse before completion/abort) and releases it afterwards.
    esp_err_t submit_capture(data::CaptureSession& capture, const features::TelemetryRecord& telemetry,
                             CommunicationMetrics& metrics, hybrid::SwitchedRailRelease release_rail = nullptr,
                             void* release_ctx = nullptr);

    // Material 12 section 49.
    esp_err_t send_test_packet(uint64_t device_id, uint32_t sequence);
    esp_err_t send_telemetry_once(const features::TelemetryRecord& record);
    // Material 13 section 47.
    esp_err_t send_capture_full(const data::CaptureSession& capture, CommunicationMetrics& metrics);

    const lorafull::LoraFullTransferStats& last_lora_full_stats() const;
    const hybrid::HybridCommunicationStats& last_hybrid_stats() const { return hybrid_.stats(); }

private:
    CommunicationMode mode_ = CommunicationMode::HYBRID_LORA_WIFI;
    hybrid::HybridCommunicationManager hybrid_;
};

CommunicationManager& communication_manager();
lorafull::LoraFullTransfer& lora_full_transfer();

}  // namespace cownect::comm
