// capture.h - one recording of all sensors, its problem checks and its summary.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "accel.h"
#include "gps.h"
#include "analog.h"

typedef struct {
    uint32_t capture_id;
    int64_t start_us, end_us;
    accel_data_t accel;
    gps_data_t gps;
    analog_data_t analog;          // microphone + temperatures
} capture_t;

// ---------------------------------------------------------------- problem flags
// Sent in the raw data header (see data_format.c). Bits 0-10 are the same as the
// full firmware; bit 11 (MIC_CLIPPED) is new in this version.
enum {
    CAP_ACCEL_OK        = 1u << 0,   // accelerometer produced samples
    CAP_GPS_UART_OK     = 1u << 1,   // GPS text is arriving (wiring/power OK)
    CAP_GPS_HAD_FIX     = 1u << 2,   // at least one valid position
    CAP_MIC_OK          = 1u << 3,   // microphone produced samples
    CAP_BOARD_TEMP_OK   = 1u << 4,
    CAP_COW_TEMP_OK     = 1u << 5,
    CAP_ADC_OK          = 1u << 6,   // ADC scan started
    CAP_ACCEL_LOST      = 1u << 7,   // accelerometer FIFO overflowed (samples lost)
    CAP_MIC_LOST        = 1u << 8,   // ADC buffer overflowed (audio samples lost)
    CAP_GPS_BAD_TEXT    = 1u << 9,   // GPS sentences with checksum errors
    CAP_COW_PROBE_FAULT = 1u << 10,  // cow readings exist but none is valid (probe unplugged/shorted?)
    CAP_MIC_CLIPPED     = 1u << 11,  // NEW: microphone signal hit the ADC limits
};

// ---------------------------------------------------------------- summary (LoRa telemetry)
typedef struct {
    bool gps_valid, board_valid, cow_valid, accel_valid, mic_valid;
    int32_t lat_e7, lon_e7;          // last valid GPS position
    float board_temp_c, cow_temp_c;  // averages of the valid values
    float accel_mean_g, accel_rms_g; // of the acceleration magnitude
    float mic_rms;                   // loudness (ADC codes, DC removed)
    uint32_t flags;                  // SUM_* bits below
} summary_t;

// Sent in the 49-byte LoRa telemetry. Bits 0-8 as in the full firmware; bit 9 is new.
enum {
    SUM_SOMETHING_LOST = 1u << 0, SUM_ACCEL_ERROR = 1u << 1, SUM_GPS_NO_FIX = 1u << 2,
    SUM_MIC_ERROR = 1u << 3, SUM_BOARD_TEMP_ERROR = 1u << 4, SUM_COW_TEMP_ERROR = 1u << 5,
    SUM_COW_PROBE_FAULT = 1u << 6, SUM_ADC_ERROR = 1u << 7, SUM_CYCLE_OVERRUN = 1u << 8,
    SUM_MIC_CLIPPED = 1u << 9,       // NEW
};

// Reserves the microphone buffer (call once after boot). False if there is no PSRAM.
bool capture_init(capture_t *c);

// Records all sensors together for CAPTURE_MS and prints a report. The sensor rail must be on.
void capture_run(capture_t *c, uint32_t capture_id);

// Works out the CAP_* problem flags for this capture.
uint32_t capture_flags(const capture_t *c);

// Works out the summary; last_cycle_overrun sets SUM_CYCLE_OVERRUN.
void capture_summary(const capture_t *c, bool last_cycle_overrun, summary_t *s);
