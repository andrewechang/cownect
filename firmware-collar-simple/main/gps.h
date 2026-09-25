// gps.h - ATGM336H GPS (UART, NMEA text). Keeps its own data in gps_data_t.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "config.h"

// ================================================================ SETTINGS
#define GPS_UART_PORT          1       // UART0 is the debug port
#define GPS_BAUD               9600    // ATGM336H default
// The module sends one position per second by default (changing that needs a
// module command, not implemented).
#define GPS_FIXES_PER_SECOND   1
// ================================================================

#define GPS_MAX_FIXES          (GPS_FIXES_PER_SECOND * (CAPTURE_MS / 1000) + 10)

typedef struct {
    uint32_t time_ms;          // time since the capture started
    bool valid;                // RMC status 'A' = the receiver has a position
    int32_t lat_e7, lon_e7;    // degrees x 10,000,000
    float speed_mps;
    float course_deg;
    uint8_t fix_quality;       // from GGA (0 = no fix)
    uint8_t satellites;        // from GGA
} gps_fix_t;

typedef struct {
    gps_fix_t fixes[GPS_MAX_FIXES];
    uint32_t count;
    bool uart_ok;              // PROBLEM CHECK: at least one good sentence arrived (wiring/power OK)
    uint32_t bad_sentences;    // PROBLEM CHECK: checksum errors / broken lines (noise, wrong baud)
} gps_data_t;

// Opens the UART to the GPS. False on failure.
bool gps_start(void);

// Reads the text that has arrived and stores one fix per RMC sentence.
void gps_read(gps_data_t *g, uint32_t time_ms);

// Closes the UART.
void gps_stop(void);
