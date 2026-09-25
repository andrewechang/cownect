// summary.h - turns the 30 s recording into a few numbers (the LoRa telemetry).
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "capture.h"

typedef struct {
    bool gps_valid, board_valid, cow_valid, accel_valid, mic_valid;
    int32_t lat_e7, lon_e7;          // last valid GPS fix
    float board_temp_c;              // mean of valid samples
    float cow_temp_c;
    float accel_mean_g;              // mean of |a|
    float accel_rms_g;               // RMS of |a|
    float mic_rms;                   // RMS of the microphone signal with the DC level removed (ADC codes)
    uint32_t status_flags;           // SUM_* bits below
} summary_t;

enum {
    SUM_CAPTURE_DEGRADED = 1u << 0, SUM_ACCEL_ERROR = 1u << 1, SUM_GPS_NO_FIX = 1u << 2,
    SUM_MIC_ERROR = 1u << 3, SUM_BOARD_TEMP_ERROR = 1u << 4, SUM_COW_TEMP_ERROR = 1u << 5,
    SUM_COW_PROBE_FAULT = 1u << 6, SUM_ADC_ERROR = 1u << 7, SUM_CYCLE_OVERRUN = 1u << 8,
};

// Computes the summary of capture c. `last_cycle_overrun` sets SUM_CYCLE_OVERRUN.
void summary_compute(const capture_t *c, bool last_cycle_overrun, summary_t *s);
