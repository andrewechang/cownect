#pragma once

#include <cstddef>
#include <cstdint>

namespace cownect::radio {

enum class LoraRegulatorMode : uint8_t { LDO, DCDC };

// Central RF profile (Material 12 section 19). No field has a default value in firmware:
// every value comes from menuconfig and is CONFIG_NOT_SET until validated.
// Addition to Material 12: sync_word, PA/ramp, regulator, TCXO and DIO2 settings, which the
// SX1262 needs and which Materials 12/16 list as unresolved values.
struct LoraRfProfile {
    uint32_t frequency_hz;
    uint32_t bandwidth_hz;
    uint8_t spreading_factor;
    uint8_t coding_rate;  // denominator: 5..8 means 4/5..4/8
    uint16_t preamble_symbols;
    int8_t tx_power_dbm;
    bool crc_enabled;
    bool invert_iq;
    uint8_t sync_word;

    uint8_t pa_duty_cycle;
    uint8_t pa_hp_max;
    uint16_t ramp_time_us;
    LoraRegulatorMode regulator;
    bool tcxo_enabled;
    uint8_t tcxo_voltage_code;  // SX126x TCXO voltage code (0..7)
    uint32_t tcxo_startup_us;
    bool dio2_rf_switch;
};

struct LoraDriverConfig {
    uint32_t spi_clock_hz;
    uint32_t busy_timeout_ms;          // DEVELOPMENT DEFAULT safety bound
    bool tx_timeout_configured;
    uint32_t tx_operation_timeout_ms;  // CONFIG_NOT_SET unless configured
    bool rx_timeout_configured;
    uint32_t rx_operation_timeout_ms;  // CONFIG_NOT_SET unless configured
};

enum class RadioState : uint8_t { UNINITIALIZED, HARDWARE_READY, STANDBY, TX_ACTIVE, RX_ACTIVE, ERROR };

enum class LoraOpResult : uint8_t { NONE, OK, TIMEOUT, CRC_ERROR, ERROR };

struct LoraPacketRx {
    size_t length;
    int16_t rssi_dbm;
    int8_t snr_db;
    int8_t signal_rssi_dbm;
};

// Material 12 section 26.
struct LoraRadioStats {
    uint32_t tx_started;
    uint32_t tx_done;
    uint32_t tx_timeout;

    uint32_t rx_done;
    uint32_t rx_timeout;
    uint32_t rx_crc_error;

    uint32_t spi_error_count;
    uint32_t busy_timeout_count;
    uint32_t reset_count;
    uint32_t irq_count;
    uint32_t unexpected_irq_count;

    uint32_t malformed_packet_count;
    uint32_t sequence_gap_count;
};

struct LoraChipStatus {
    uint8_t chip_mode;   // SX126x chip mode code (2 = STBY_RC, 3 = STBY_XOSC, 4 = FS, 5 = RX, 6 = TX)
    uint8_t cmd_status;  // SX126x command status code
    uint16_t device_errors;
    bool busy_level;
    uint32_t last_busy_wait_us;
};

const char* radio_state_name(RadioState s);
const char* lora_chip_mode_name(uint8_t mode);

}  // namespace cownect::radio
