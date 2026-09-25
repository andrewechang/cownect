// lora.h - minimal SX1262 (Ra-01SH) driver.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "config.h"

// ================================================================ SETTINGS
// TODO: all of these must be decided for your region / module before any transmission.
// They must match the receiver (Heltec gateway) exactly.
#define LORA_FREQUENCY_HZ      0       // TODO e.g. 923000000 - 0 = not set
#define LORA_BANDWIDTH_KHZ     0       // TODO 125, 250 or 500 - 0 = not set
#define LORA_SPREADING_FACTOR  0       // TODO 7..12 - 0 = not set
#define LORA_CODING_RATE       5       // TODO 5..8 meaning 4/5..4/8 - 0 = not set
#define LORA_TX_POWER_DBM      0       // TODO -9..22 (module max +22) and within your legal limit - 0 = not set
#define LORA_SYNC_WORD         0       // TODO 0x1424 (private) or 0x3444 (public) - 0 = not set
#define LORA_PREAMBLE_LEN      8
// --- Module hardware facts, from the Ra-01SH datasheet V1.1 (RA01SH.pdf, schematic p.11) ---
#define LORA_USE_TCXO          0       // Ra-01SH: passive crystal Y1 on XTA/XTB, DIO3 not used for a TCXO
#define LORA_TCXO_VOLTAGE      0x02    // not used (no TCXO)
#define LORA_USE_DIO2_RF_SWITCH 1      // Ra-01SH: DIO2 drives the RF switch U2 (DIO2=1 TX, DIO2=0 RX)
#define LORA_USE_DCDC          1       // Ra-01SH: 15 uH inductor L6 fitted on DCC_SW, so DC-DC works
                                       // (lower current); 0 = LDO also works
// ---------------------------------------------------------------------------------------------
#define LORA_TX_TIMEOUT_MS     0       // TODO e.g. 3000 - 0 = not set
#define LORA_SPI_HZ            8000000 // SPI clock to the radio (Ra-01SH max 10 MHz)
// Set to 1 ONLY after checking the external antenna is attached. Transmitting
// without an antenna can damage the radio.
#define LORA_ANTENNA_CONNECTED 0
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
