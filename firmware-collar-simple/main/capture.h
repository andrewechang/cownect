// capture.h - everything recorded during one capture.
// Buffer sizes come from the sensor settings in accel.h, gps.h and analog.h.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "config.h"
#include "accel.h"
#include "gps.h"
#include "analog.h"

typedef struct {
    int16_t x, y, z;            // raw LIS2DW12 output words (14-bit value, left-justified)
} accel_sample_t;

typedef struct {
    uint32_t offset_ms;         // time since capture start
    int32_t lat_e7;             // degrees x 1e7
    int32_t lon_e7;
    float speed_mps;
    float course_deg;
    uint8_t fix_quality;        // from GGA: 0 = no fix
    uint8_t satellites;
    bool valid;                 // RMC status 'A'
} gps_fix_t;

typedef struct {
    uint32_t ts_ms;
    int32_t adc_raw;            // average of the raw ADC codes over TEMP_REPORT_MS
    int32_t mv;                 // calibrated millivolts (-1 = no calibration)
    float resistance_ohm;       // cow thermistor only, NAN for the board sensor
    float temp_c;               // NAN when the reading is invalid
    bool valid;
} temp_sample_t;

struct capture_s {
    uint32_t capture_id;
    int64_t start_us, end_us;

    accel_sample_t accel[ACCEL_MAX_SAMPLES];
    uint32_t accel_count;
    bool accel_ok;              // sensor found and configured
    uint32_t accel_overruns;    // FIFO overflowed (samples lost)

    gps_fix_t gps[GPS_MAX_FIXES];
    uint32_t gps_count;
    bool gps_uart_ok;           // at least one good NMEA sentence was received
    uint32_t gps_bad_sentences;

    uint16_t *mic;              // points into PSRAM, MIC_MAX_SAMPLES long
    uint32_t mic_count;

    temp_sample_t board_temp[TEMP_MAX_SAMPLES];
    uint32_t board_temp_count;
    temp_sample_t cow_temp[TEMP_MAX_SAMPLES];
    uint32_t cow_temp_count;

    bool adc_ok;                // continuous ADC started and ran
    uint32_t adc_overflows;     // ADC DMA pool overflowed (samples lost)
};

// Clears the capture and allocates the microphone buffer in PSRAM (call once).
// Returns false if there is not enough PSRAM.
bool capture_init(capture_t *c);

// Records all sensors together for CAPTURE_MS and logs a short report.
// The sensor power rail must already be on.
void capture_run(capture_t *c, uint32_t capture_id);

// Bits for capture_status_flags() (same meaning as the full firmware's stream header).
enum {
    CAP_ACCEL_OK = 1u << 0, CAP_GPS_UART_OK = 1u << 1, CAP_GPS_HAD_FIX = 1u << 2,
    CAP_MIC_OK = 1u << 3, CAP_BOARD_TEMP_OK = 1u << 4, CAP_COW_TEMP_OK = 1u << 5,
    CAP_ADC_OK = 1u << 6, CAP_ACCEL_DEGRADED = 1u << 7, CAP_MIC_DEGRADED = 1u << 8,
    CAP_GPS_DEGRADED = 1u << 9, CAP_COW_PROBE_FAULT = 1u << 10,
};

// Returns the CAP_* bits describing which sensors worked in this capture.
uint32_t capture_status_flags(const capture_t *c);
