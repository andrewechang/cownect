#pragma once

#include <cstdint>
#include "capture_types.h"
#include "esp_err.h"
#include "telemetry_record.h"
#include "wifi_capture_uploader.h"

namespace cownect::hybrid {

struct HybridPrototypeConfig {
    bool enable_lora_telemetry;
    bool enable_wifi_raw_upload;
};
HybridPrototypeConfig hybrid_config_from_menuconfig();

enum class HybridState { IDLE, SENDING_LORA, STARTING_WIFI, UPLOADING_WIFI, CLEANUP, COMPLETE, FAILED };
enum class HybridResult { BOTH_OK, LORA_OK_WIFI_FAILED, LORA_FAILED_WIFI_OK, BOTH_FAILED };
const char* hybrid_result_name(HybridResult r);

// Material 15 section 15.
struct HybridCommunicationStats {
    uint32_t capture_id;

    bool lora_attempted;
    bool lora_success;
    uint32_t lora_bytes;
    uint32_t lora_time_ms;
    esp_err_t lora_error;

    bool wifi_attempted;
    bool wifi_success;
    uint32_t wifi_bytes;
    uint32_t wifi_time_ms;
    upload::WifiUploadStats wifi;

    uint32_t total_communication_time_ms;
    HybridResult result;
};

// Called after the LoRa step when no switched-rail user remains (Material 15 section 22).
// Owned by the scheduler/power manager; this module never touches GPIO4 itself.
using SwitchedRailRelease = void (*)(void* ctx);

// Orchestration only: LoRa telemetry first, then Wi-Fi raw upload, sequentially. One transport
// failing never cancels the other. Never enters deep sleep.
class HybridCommunicationManager {
public:
    esp_err_t run(const data::CaptureSession& capture, const features::TelemetryRecord& telemetry,
                  SwitchedRailRelease release_rail = nullptr, void* release_ctx = nullptr);
    const HybridCommunicationStats& stats() const { return stats_; }
    HybridState state() const { return state_; }

private:
    HybridCommunicationStats stats_ = {};
    HybridState state_ = HybridState::IDLE;
};

}  // namespace cownect::hybrid
