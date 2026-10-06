// lora.h - minimal SX1262 (Ra-01SH) driver.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "config.h"

// ================================================================ SETTINGS
// US915 single-channel profile, the same as the full firmware's test_profile.h.
// They must match the receiver (Heltec gateway) exactly.
// 500 kHz: the bandwidth FCC Part 15.247 asks for on ONE fixed channel (no frequency hopping).
// SF10 at 500 kHz reaches about as far as SF8 at 125 kHz; a 49-byte packet is ~144 ms on air.
// Need more range? SF11 (~250 ms) or SF12 (~535 ms), and the gateway must change too.
#define LORA_FREQUENCY_HZ      915000000
#define LORA_BANDWIDTH_KHZ     500     // 125, 250 or 500
#define LORA_SPREADING_FACTOR  10      // 7..12 (SF10/500 kHz: a 255 B packet is ~0.57 s on air)
#define LORA_CODING_RATE       5       // 5..8 meaning 4/5..4/8
#define LORA_TX_POWER_DBM      22      // -9..22: +22 = module maximum, for range through the cow's body
#define LORA_SYNC_WORD         0x1424  // private network (= sync word 0x12 on the gateway)
#define LORA_PREAMBLE_LEN      8
// --- Module hardware facts, from the Ra-01SH datasheet V1.1 (RA01SH.pdf, schematic p.11) ---
#define LORA_USE_TCXO          0       // Ra-01SH: passive crystal Y1 on XTA/XTB, DIO3 not used for a TCXO
#define LORA_TCXO_VOLTAGE      0x02    // not used (no TCXO)
#define LORA_USE_DIO2_RF_SWITCH 1      // Ra-01SH: DIO2 drives the RF switch U2 (DIO2=1 TX, DIO2=0 RX)
#define LORA_USE_DCDC          1       // Ra-01SH: 15 uH inductor L6 fitted on DCC_SW, so DC-DC works
                                       // (lower current); 0 = LDO also works
// ---------------------------------------------------------------------------------------------
#define LORA_TX_TIMEOUT_MS     3000
#define LORA_SPI_HZ            8000000 // SPI clock to the radio (Ra-01SH max 10 MHz)
// Set to 1 ONLY after checking the external antenna is attached. Transmitting
// without an antenna can damage the radio.
#define LORA_ANTENNA_CONNECTED 1       // the board uses an external antenna
// ================================================================

#define LORA_MAX_PACKET 255     // SX1262 payload length is one byte

// Returns true when every setting above is filled in and the antenna is confirmed;
// otherwise logs the first missing setting and returns false.
bool lora_config_ready(void);

// Wiring check without transmitting: resets the radio and reads the sync-word
// register, which must read 0x1424 after reset. Returns true if it does.
bool lora_spi_check(void);

// Starts the SPI bus, resets the radio and applies all settings.
// Returns false if the settings are not ready or the radio does not respond.
bool lora_begin(void);

// Sends one packet (1..255 bytes) and waits for "TX done" (max LORA_TX_TIMEOUT_MS).
bool lora_transmit(const uint8_t *data, size_t len);

// Listens for one packet for up to timeout_ms. Returns the number of bytes
// copied into buf, or -1 on timeout or a damaged packet (CRC/header error).
int lora_receive(uint8_t *buf, size_t max, uint32_t timeout_ms);

// Puts the radio in standby and releases the SPI bus.
void lora_end(void);

// Sends one packet in one call: lora_begin() + lora_transmit() + lora_end().
bool lora_send(const uint8_t *data, size_t len);
