#pragma once

// Built-in bring-up test profile (user, 2026-10-05).
//
// Any of these settings left empty in menuconfig takes the value below, so the PCB tests run
// without editing menuconfig. A value set in menuconfig always wins.
//
// LoRa: US915 single-channel profile. The Heltec gateway (Material 16) must use the same
// frequency, bandwidth, SF, CR, preamble, sync word, CRC and IQ values.
// 500 kHz: the bandwidth FCC Part 15.247 asks for on ONE fixed channel (no frequency hopping).
// SF10 at 500 kHz reaches about as far as SF8 at 125 kHz; a 49-byte telemetry packet is ~144 ms
// on air. More range: SF11 (~250 ms) or SF12 (~535 ms), with the same change on the gateway.

#include "config_parse.h"

namespace cownect::config::test_profile {

// ---- LoRa RF profile -----------------------------------------------------------------------
inline constexpr const char* LORA_FREQUENCY_HZ = "915000000";
inline constexpr const char* LORA_BANDWIDTH_HZ = "500000";
inline constexpr const char* LORA_SPREADING_FACTOR = "10";
inline constexpr const char* LORA_CODING_RATE = "5";  // 4/5
inline constexpr const char* LORA_PREAMBLE_SYMBOLS = "8";
inline constexpr const char* LORA_SYNC_WORD = "0x12";  // private network
inline constexpr const char* LORA_CRC_ENABLED = "1";
inline constexpr const char* LORA_INVERT_IQ = "0";
// SX1262 datasheet optimal PA table, +22 dBm row: paDutyCycle 0x04, hpMax 0x07, SetTxParams +22.
// Module maximum, for range through the cow's body (~120 mA from 3.3 V while transmitting).
inline constexpr const char* LORA_TX_POWER_DBM = "22";
inline constexpr const char* LORA_PA_DUTY_CYCLE = "4";
inline constexpr const char* LORA_PA_HP_MAX = "7";
inline constexpr const char* LORA_RAMP_TIME_US = "200";
// SF10 / 500 kHz: a 255 B packet is ~0.57 s on air.
inline constexpr const char* LORA_TX_TIMEOUT_MS = "3000";
inline constexpr const char* LORA_RX_TIMEOUT_MS = "5000";
// The board uses an external antenna (user, 2026-09-24). RF TX tests still print the antenna
// warning and a 3 s countdown before transmitting.
inline constexpr bool LORA_ANTENNA_VERIFIED = true;

// ---- LORA_FULL -----------------------------------------------------------------------------
inline constexpr const char* LORA_FULL_FRAGMENT_PAYLOAD_BYTES = "200";  // 40 B header + 200 <= 255

// ---- GPS -----------------------------------------------------------------------------------
// Rates the GPS tests try, in order, until valid NMEA sentences are framed.
inline constexpr uint32_t GPS_BAUD_CANDIDATES[] = {9600, 115200, 38400, 57600, 19200, 4800};

// Returns the menuconfig value when set, otherwise the built-in test value.
inline const char* pick(const char* menuconfig_value, const char* builtin)
{
    return is_set(menuconfig_value) ? menuconfig_value : builtin;
}

}  // namespace cownect::config::test_profile
