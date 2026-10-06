#pragma once

#include <cstddef>
#include "esp_err.h"
#include "lora_types.h"

namespace cownect::radio {

// Human-readable list of missing/invalid configuration names.
struct LoraConfigReport {
    char missing[320];
    bool uses_test_profile;  // at least one value came from the built-in test profile
};

// Loads the RF profile from menuconfig; empty fields take the built-in test profile
// (test_profile.h). Returns COWNECT_ERR_NOT_CONFIGURED and lists every invalid value.
esp_err_t lora_load_rf_profile(LoraRfProfile& out, LoraConfigReport& report);

// SPI clock / BUSY bound are always available; TX/RX timeouts are flagged when not set.
void lora_load_driver_config(LoraDriverConfig& out, LoraConfigReport& report);

// RF TX gate: RF profile complete + TX timeout configured + operator antenna confirmation.
// Returns ESP_OK or COWNECT_ERR_NOT_CONFIGURED / COWNECT_ERR_TX_BLOCKED with a reason.
esp_err_t lora_tx_gate(const char** reason);

// Maps a supported LoRa bandwidth in Hz. Returns false for unsupported values.
bool lora_bandwidth_supported(uint32_t hz);

}  // namespace cownect::radio
