#pragma once

#include "esp_err.h"
#include "lora_types.h"

namespace cownect::radio {

// Byte-oriented radio API (Material 12 section 14). Knows nothing about sensors, fragments,
// telemetry or retry policy.
class ILoraRadio {
public:
    virtual ~ILoraRadio() = default;

    virtual esp_err_t init(const LoraRfProfile& profile) = 0;
    virtual esp_err_t enter_standby() = 0;

    // Non-blocking start. Refused unless the RF TX gate (profile + antenna confirmation) passes.
    virtual esp_err_t send_async(const uint8_t* data, size_t length) = 0;
    virtual esp_err_t start_receive(uint32_t timeout_ms) = 0;
    // Handles pending DIO1 events (IRQ read/clear over SPI happens here, never in the ISR).
    virtual esp_err_t service() = 0;

    virtual bool tx_in_progress() const = 0;
    virtual bool rx_in_progress() const = 0;

    virtual const LoraRadioStats& stats() const = 0;
};

}  // namespace cownect::radio
