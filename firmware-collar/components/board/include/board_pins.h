#pragma once

// CowNect Collar - current schematic updated 2026-09-06 (Material 2 Rev C section 19,
// Material 4 section 4). The ONLY place where PCB GPIO numbers appear.
//
// Unassigned GPIOs (3, 8, 16, 35-42, 45-48) are intentionally not listed:
// they must not be repurposed without explicit hardware approval.

namespace cownect::board {

static constexpr int PIN_BOOT           = 0;   // BOOT button, R21 10k pull-up
static constexpr int PIN_BOARD_TEMP_ADC = 1;   // ADC1_CH0, MCP9700 via R9/C14
static constexpr int PIN_COW_TEMP_ADC   = 2;   // ADC1_CH1, MF58 divider via R10/C15
static constexpr int PIN_PERIPH_EN      = 4;   // TPS22919 ON path (via ECO), R2 100k pulldown
static constexpr int PIN_MIC_ADC        = 5;   // ADC1_CH4, MCP6001 output via R18/C20
static constexpr int PIN_I2C_SDA        = 6;   // LIS2DW12 SDA, R19 4.7k pull-up
static constexpr int PIN_I2C_SCL        = 7;   // LIS2DW12 SCL, R3 4.7k pull-up

static constexpr int PIN_LORA_CS        = 9;   // Ra-01SH NSS
static constexpr int PIN_LORA_MOSI      = 10;
static constexpr int PIN_LORA_SCK       = 11;
static constexpr int PIN_LORA_MISO      = 12;
static constexpr int PIN_LORA_RST       = 13;
static constexpr int PIN_LORA_DIO1      = 14;

static constexpr int PIN_MODE_SW        = 15;  // PROG switch through R22 10k series

static constexpr int PIN_GPS_UART_RX    = 17;  // connected to GPS TXD (net GPS_TX)
static constexpr int PIN_GPS_UART_TX    = 18;  // connected to GPS RXD (net GPS_RX)

static constexpr int PIN_USB_DM         = 19;  // native USB, reserved
static constexpr int PIN_USB_DP         = 20;  // native USB, reserved

static constexpr int PIN_LORA_BUSY      = 21;

// Ra-01SH TXEN, RXEN, DIO2, DIO3 are NOT connected to the ESP32 on this PCB.
// TXD0/RXD0 are reserved for the backup programming/debug UART (see board_uart.h).

}  // namespace cownect::board
