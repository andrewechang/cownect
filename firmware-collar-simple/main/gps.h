// gps.h - ATGM336H GPS over UART (NMEA text).
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "config.h"

// ================================================================ SETTINGS
#define GPS_UART_PORT          1       // ESP32 UART used for the GPS (UART0 is the debug port)
#define GPS_BAUD               9600    // must match the module (ATGM336H default: 9600)
#define GPS_RX_BUFFER_BYTES    4096    // UART receive buffer
// The module sends 1 position per second by default. Changing its update rate
// needs a configuration command to the module (not implemented; see CHECKLIST).
#define GPS_FIXES_PER_SECOND   1
// ================================================================

// Room for the whole capture plus 10 fixes of margin
#define GPS_MAX_FIXES          (GPS_FIXES_PER_SECOND * (CAPTURE_MS / 1000) + 10)

typedef struct capture_s capture_t;

// Installs the UART driver on the GPS pins. Returns false on failure.
bool gps_start(void);

// Reads all bytes that have arrived, builds NMEA sentences and stores one fix in
// the capture for every RMC sentence. `now_ms` = time since capture start.
void gps_poll(capture_t *c, uint32_t now_ms);

// Removes the UART driver.
void gps_stop(void);
