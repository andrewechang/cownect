#pragma once

#include "driver/uart.h"
#include "cownect_config.h"

namespace cownect::board {

// Backup programming/debug UART: ESP32 UART0 on its default TXD0/RXD0 pins (Material 2
// section 13.2). Routing is never changed by firmware. It is not a Jetson data link.
static constexpr uart_port_t BACKUP_DEBUG_UART = UART_NUM_0;

// GPS UART controller: explicit design decision kept in configuration (Material 7 stage 7.1).
static constexpr uart_port_t GPS_UART = static_cast<uart_port_t>(config::GPS_UART_PORT);

static_assert(config::GPS_UART_PORT != 0, "UART0 is reserved for backup debug");

}  // namespace cownect::board
