// analog.h - microphone + board temperature + cow temperature, all on ADC1.
//
// The ADC repeats the pattern MIC, BOARD, MIC, COW, so:
//   microphone        = MIC_SAMPLE_RATE_HZ, every sample kept
//   each temperature  = MIC_SAMPLE_RATE_HZ / 2 readings/s, averaged to one value per TEMP_EVERY_MS
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "config.h"

// ================================================================ SETTINGS
#define MIC_SAMPLE_RATE_HZ     32000   // microphone rate (baseline 32000, max 41000)
#define TEMP_EVERY_MS          1000    // one temperature value every ... ms (baseline 1000)
#define ADC_ATTEN_SETTING      ADC_ATTEN_DB_12   // input range ~0-3.1 V (provisional)

// Microphone clipping: a sample at the very bottom or top of the ADC range means the
// signal was too loud and was cut off.
#define MIC_CLIP_LOW           0
#define MIC_CLIP_HIGH          4095    // 12-bit maximum

// Temperature sensor constants (datasheets / parts list)
#define MCP9700_MV_AT_0C       500.0f
#define MCP9700_MV_PER_C       10.0f
#define NTC_R25_OHM            10000.0f   // MF58 thermistor: 10 k at 25 C
#define NTC_BETA               3950.0f
#define NTC_FIXED_R_OHM        10000.0f   // other resistor in the divider
#define NTC_SUPPLY_MV          3300.0f    // divider supply (nominal, not measured)
// ================================================================

#define ADC_TOTAL_HZ           (2 * MIC_SAMPLE_RATE_HZ)
#if ADC_TOTAL_HZ > 83333 || ADC_TOTAL_HZ < 611
  #error "MIC_SAMPLE_RATE_HZ out of range: the ESP32-S3 ADC runs at 611..83333 Hz in total"
#endif
#define MIC_MAX_SAMPLES        (MIC_SAMPLE_RATE_HZ * (CAPTURE_MS / 1000) + MIC_SAMPLE_RATE_HZ / 10)
#define TEMP_MAX_SAMPLES       (CAPTURE_MS / TEMP_EVERY_MS + 2)

typedef struct {
    uint32_t time_ms;
    int32_t adc_raw;           // average raw ADC code over TEMP_EVERY_MS
    int32_t mv;                // calibrated millivolts (-1 = no calibration)
    float resistance_ohm;      // cow thermistor only (NAN for the board sensor)
    float temp_c;              // NAN when invalid
    bool valid;
} temp_sample_t;

typedef struct {
    uint16_t *mic;             // PSRAM buffer, MIC_MAX_SAMPLES long
    uint32_t mic_count;
    uint32_t mic_clipped;      // PROBLEM CHECK: samples at 0 or 4095 (sound too loud, cut off)
    uint32_t overflows;        // PROBLEM CHECK: ADC buffer overflowed -> samples lost
    bool ok;                   // ADC started

    temp_sample_t board[TEMP_MAX_SAMPLES];
    uint32_t board_count;
    temp_sample_t cow[TEMP_MAX_SAMPLES];
    uint32_t cow_count;
} analog_data_t;

// Reserves the microphone buffer in PSRAM (call once after boot). False if no PSRAM.
bool analog_init(analog_data_t *d);

// Starts the ADC scan. False on failure.
bool analog_start(analog_data_t *d);

// Takes the next block of ADC results (~4 ms of data) and sorts it into mic / temperatures.
void analog_read(analog_data_t *d);

// Stops the ADC and finishes the last temperature value.
void analog_stop(analog_data_t *d);
