#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lora_radio.h"

namespace cownect::radio {

struct Sx1262HalContext;

// CowNect adapter over the vendored Semtech driver (Material 12 section 7):
//   ILoraRadio -> Sx1262Adapter -> sx126x_driver -> ESP-IDF HAL (SPI, RESET, BUSY, DIO1).
// TXEN/RXEN/DIO2/DIO3 are not wired to the ESP32 and no GPIO is assigned to them.
// PERIPH_EN is never touched here: the power manager owns the rail.
class Sx1262Adapter final : public ILoraRadio {
public:
    // SPI bus/device, GPIO directions, DIO1 ISR. No RF profile needed (non-radiating).
    esp_err_t init_hardware();
    // Hardware reset + bounded BUSY wait. Non-radiating.
    esp_err_t hardware_reset();
    // GetStatus + GetDeviceErrors. Non-radiating. Requires init_hardware().
    esp_err_t read_status(LoraChipStatus& out);
    // Standby (RC) command without an RF profile (non-radiating smoke test).
    esp_err_t standby_without_profile();

    esp_err_t init(const LoraRfProfile& profile) override;
    esp_err_t enter_standby() override;
    esp_err_t send_async(const uint8_t* data, size_t length) override;
    esp_err_t start_receive(uint32_t timeout_ms) override;
    esp_err_t service() override;
    bool tx_in_progress() const override { return state_ == RadioState::TX_ACTIVE; }
    bool rx_in_progress() const override { return state_ == RadioState::RX_ACTIVE; }
    const LoraRadioStats& stats() const override;

    // Blocks up to timeout_ms for a DIO1 event (or the software watchdog), then services it.
    esp_err_t wait_for_event(uint32_t timeout_ms);
    // Convenience: send and wait bounded for completion. Returns ESP_OK only on TX_DONE.
    esp_err_t send_blocking(const uint8_t* data, size_t length, uint32_t& elapsed_ms);

    // Copies the last received packet; returns false if none is pending.
    bool take_last_rx(uint8_t* dst, size_t capacity, LoraPacketRx& meta);
    LoraOpResult last_tx_result() const { return last_tx_result_; }
    LoraOpResult last_rx_result() const { return last_rx_result_; }
    RadioState state() const { return state_; }
    bool profile_applied() const { return profile_applied_; }
    bool busy_level() const;

    void note_malformed_packet() { stats_.malformed_packet_count++; }
    void note_sequence_gap() { stats_.sequence_gap_count++; }
    void reset_stats();

    // After SWITCHED_3V3 was commanded off: radio state is lost, re-init required.
    void mark_power_lost();
    // Releases SPI device/bus and ISR.
    esp_err_t deinit();

private:
    static void dio1_isr(void* arg);
    esp_err_t check(int sx_status);
    esp_err_t apply_packet_params(uint8_t payload_len);
    void to_standby_after_event();

    Sx1262HalContext* hal_ = nullptr;
    SemaphoreHandle_t irq_sem_ = nullptr;
    volatile uint32_t isr_count_ = 0;
    bool hw_ready_ = false;
    bool profile_applied_ = false;
    LoraRfProfile profile_ = {};
    RadioState state_ = RadioState::UNINITIALIZED;
    LoraOpResult last_tx_result_ = LoraOpResult::NONE;
    LoraOpResult last_rx_result_ = LoraOpResult::NONE;
    int64_t op_start_us_ = 0;
    uint32_t op_timeout_ms_ = 0;
    mutable LoraRadioStats stats_ = {};

    uint8_t rx_buf_[255] = {};
    LoraPacketRx rx_meta_ = {};
    bool rx_pending_ = false;
};

Sx1262Adapter& lora_radio();

// Loads the configured RF profile and applies it if not yet applied (e.g. after a switched-rail
// power cycle). COWNECT_ERR_NOT_CONFIGURED when the profile is incomplete. Non-radiating.
esp_err_t lora_radio_prepare_from_config();

}  // namespace cownect::radio
