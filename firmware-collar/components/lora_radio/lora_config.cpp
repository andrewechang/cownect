#include "lora_config.h"

#include <cstdio>
#include <cstring>
#include "config_parse.h"
#include "cownect_err.h"
#include "sdkconfig.h"

namespace cownect::radio {
namespace {

constexpr uint32_t kSupportedBw[] = {7810, 10420, 15630, 20830, 31250, 41670, 62500, 125000, 250000, 500000};

void note(LoraConfigReport& r, const char* name)
{
    const size_t used = std::strlen(r.missing);
    std::snprintf(r.missing + used, sizeof(r.missing) - used, "%s%s", used ? "," : "", name);
}

template <typename T>
bool load_int(const char* text, int64_t lo, int64_t hi, T& out, LoraConfigReport& r, const char* name)
{
    int64_t v = 0;
    if (!config::parse_int(text, lo, hi, v)) {
        note(r, name);
        return false;
    }
    out = static_cast<T>(v);
    return true;
}

bool load_bool(const char* text, bool& out, LoraConfigReport& r, const char* name)
{
    if (!config::parse_bool01(text, out)) {
        note(r, name);
        return false;
    }
    return true;
}

}  // namespace

bool lora_bandwidth_supported(uint32_t hz)
{
    for (uint32_t bw : kSupportedBw) {
        if (bw == hz) return true;
    }
    return false;
}

esp_err_t lora_load_rf_profile(LoraRfProfile& p, LoraConfigReport& r)
{
    p = {};
    r.missing[0] = '\0';
    bool ok = true;
    // Frequency: SX1262 synthesizer range 150-960 MHz (datasheet). The legal channel and the
    // module/gateway band variant must be confirmed separately (hardware_questions.md).
    ok &= load_int(CONFIG_COWNECT_LORA_FREQUENCY_HZ, 150000000, 960000000, p.frequency_hz, r, "FREQUENCY_HZ");
    if (load_int(CONFIG_COWNECT_LORA_BANDWIDTH_HZ, 7810, 500000, p.bandwidth_hz, r, "BANDWIDTH_HZ")) {
        if (!lora_bandwidth_supported(p.bandwidth_hz)) {
            note(r, "BANDWIDTH_HZ(unsupported)");
            ok = false;
        }
    } else {
        ok = false;
    }
    ok &= load_int(CONFIG_COWNECT_LORA_SPREADING_FACTOR, 5, 12, p.spreading_factor, r, "SPREADING_FACTOR");
    ok &= load_int(CONFIG_COWNECT_LORA_CODING_RATE, 5, 8, p.coding_rate, r, "CODING_RATE");
    ok &= load_int(CONFIG_COWNECT_LORA_PREAMBLE_SYMBOLS, 1, 65535, p.preamble_symbols, r, "PREAMBLE_SYMBOLS");
    ok &= load_int(CONFIG_COWNECT_LORA_SYNC_WORD, 0, 255, p.sync_word, r, "SYNC_WORD");
    ok &= load_bool(CONFIG_COWNECT_LORA_CRC_ENABLED, p.crc_enabled, r, "CRC_ENABLED");
    ok &= load_bool(CONFIG_COWNECT_LORA_INVERT_IQ, p.invert_iq, r, "INVERT_IQ");
    ok &= load_int(CONFIG_COWNECT_LORA_TX_POWER_DBM, -9, 22, p.tx_power_dbm, r, "TX_POWER_DBM");
    ok &= load_int(CONFIG_COWNECT_LORA_PA_DUTY_CYCLE, 0, 7, p.pa_duty_cycle, r, "PA_DUTY_CYCLE");
    ok &= load_int(CONFIG_COWNECT_LORA_PA_HP_MAX, 0, 7, p.pa_hp_max, r, "PA_HP_MAX");

    int64_t ramp = 0;
    if (config::parse_int(CONFIG_COWNECT_LORA_RAMP_TIME_US, 10, 3400, ramp) &&
        (ramp == 10 || ramp == 20 || ramp == 40 || ramp == 80 || ramp == 200 || ramp == 800 || ramp == 1700 ||
         ramp == 3400)) {
        p.ramp_time_us = static_cast<uint16_t>(ramp);
    } else {
        note(r, "RAMP_TIME_US");
        ok = false;
    }

    const char* reg = CONFIG_COWNECT_LORA_REGULATOR_MODE;
    if (std::strcmp(reg, "LDO") == 0) {
        p.regulator = LoraRegulatorMode::LDO;
    } else if (std::strcmp(reg, "DCDC") == 0) {
        p.regulator = LoraRegulatorMode::DCDC;
    } else {
        note(r, "REGULATOR_MODE");
        ok = false;
    }

    static const char* kTcxo[] = {"1.6", "1.7", "1.8", "2.2", "2.4", "2.7", "3.0", "3.3"};
    const char* tcxo = CONFIG_COWNECT_LORA_TCXO;
    if (std::strcmp(tcxo, "NONE") == 0) {
        p.tcxo_enabled = false;
    } else {
        bool found = false;
        for (uint8_t i = 0; i < 8; ++i) {
            if (std::strcmp(tcxo, kTcxo[i]) == 0) {
                p.tcxo_enabled = true;
                p.tcxo_voltage_code = i;
                found = true;
            }
        }
        if (!found) {
            note(r, "TCXO");
            ok = false;
        } else {
            ok &= load_int(CONFIG_COWNECT_LORA_TCXO_STARTUP_US, 1, 262000000, p.tcxo_startup_us, r, "TCXO_STARTUP_US");
        }
    }
    ok &= load_bool(CONFIG_COWNECT_LORA_DIO2_RF_SWITCH, p.dio2_rf_switch, r, "DIO2_RF_SWITCH");
    return ok ? ESP_OK : COWNECT_ERR_NOT_CONFIGURED;
}

void lora_load_driver_config(LoraDriverConfig& d, LoraConfigReport& r)
{
    d = {};
    r.missing[0] = '\0';
    d.spi_clock_hz = CONFIG_COWNECT_LORA_SPI_CLOCK_HZ;
    d.busy_timeout_ms = CONFIG_COWNECT_LORA_BUSY_TIMEOUT_MS;
    d.tx_timeout_configured =
        load_int(CONFIG_COWNECT_LORA_TX_TIMEOUT_MS, 1, 262143, d.tx_operation_timeout_ms, r, "TX_TIMEOUT_MS");
    d.rx_timeout_configured =
        load_int(CONFIG_COWNECT_LORA_RX_TIMEOUT_MS, 1, 262143, d.rx_operation_timeout_ms, r, "RX_TIMEOUT_MS");
}

esp_err_t lora_tx_gate(const char** reason)
{
    LoraRfProfile p;
    LoraConfigReport r;
    if (lora_load_rf_profile(p, r) != ESP_OK) {
        if (reason) *reason = "RF profile CONFIG_NOT_SET";
        return COWNECT_ERR_NOT_CONFIGURED;
    }
    LoraDriverConfig d;
    lora_load_driver_config(d, r);
    if (!d.tx_timeout_configured) {
        if (reason) *reason = "TX operation timeout CONFIG_NOT_SET";
        return COWNECT_ERR_NOT_CONFIGURED;
    }
#if CONFIG_COWNECT_LORA_ANTENNA_VERIFIED
    if (reason) *reason = "ok";
    return ESP_OK;
#else
    if (reason) *reason = "external antenna connection not confirmed (set COWNECT_LORA_ANTENNA_VERIFIED in menuconfig)";
    return COWNECT_ERR_TX_BLOCKED;
#endif
}

const char* radio_state_name(RadioState s)
{
    switch (s) {
    case RadioState::UNINITIALIZED:  return "UNINITIALIZED";
    case RadioState::HARDWARE_READY: return "HARDWARE_READY";
    case RadioState::STANDBY:        return "STANDBY";
    case RadioState::TX_ACTIVE:      return "TX_ACTIVE";
    case RadioState::RX_ACTIVE:      return "RX_ACTIVE";
    case RadioState::ERROR:          return "ERROR";
    }
    return "?";
}

const char* lora_chip_mode_name(uint8_t mode)
{
    switch (mode) {
    case 2: return "STBY_RC";
    case 3: return "STBY_XOSC";
    case 4: return "FS";
    case 5: return "RX";
    case 6: return "TX";
    default: return "UNKNOWN";
    }
}

}  // namespace cownect::radio
