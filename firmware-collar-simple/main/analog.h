// analog.h - microphone + both temperature sensors on ADC1, sampled together.
//
// The ADC scans MIC, BOARD_TEMP, MIC, COW_TEMP over and over, so:
//   total ADC rate     = 2 x MIC_SAMPLE_RATE_HZ
//   microphone         = MIC_SAMPLE_RATE_HZ, every sample stored
//   each temperature   = MIC_SAMPLE_RATE_HZ / 2, averaged to one value per TEMP_REPORT_MS
#pragma once
#include <stdbool.h>
#include "config.h"

// ================================================================ SETTINGS
#define MIC_SAMPLE_RATE_HZ     32000   // microphone sample rate (baseline 32000; max 41000)
#define TEMP_REPORT_MS         1000    // one averaged temperature value every ... ms (baseline 1000)
#define ADC_ATTEN_SETTING      ADC_ATTEN_DB_12   // input range ~0-3.1 V (provisional bring-up value)

#define ADC_FRAME_BYTES        1024    // data taken from the driver per analog_poll() (4 bytes per result)
#define ADC_POOL_BYTES         32768   // driver buffer; at 64 kHz this is ~125 ms of data

// Temperature sensor constants (datasheet / BOM values)
#define MCP9700_V0_MV          500.0f  // board sensor output at 0 C
#define MCP9700_MV_PER_C       10.0f
#define NTC_R25_OHM            10000.0f   // cow thermistor MF58: 10 k at 25 C
#define NTC_BETA               3950.0f
#define NTC_FIXED_R_OHM        10000.0f   // fixed resistor in the divider
#define NTC_SUPPLY_MV          3300.0f    // divider supply (nominal, not measured)
// ================================================================

// Derived values - no need to edit below this line.
#define ADC_TOTAL_HZ           (2 * MIC_SAMPLE_RATE_HZ)
#if ADC_TOTAL_HZ > 83333 || ADC_TOTAL_HZ < 611
  #error "MIC_SAMPLE_RATE_HZ out of range: the ESP32-S3 ADC runs at 611..83333 Hz in total"
#endif
#define MIC_MAX_SAMPLES        (MIC_SAMPLE_RATE_HZ * (CAPTURE_MS / 1000) + MIC_SAMPLE_RATE_HZ / 10)
#define TEMP_MAX_SAMPLES       (CAPTURE_MS / TEMP_REPORT_MS + 2)

typedef struct capture_s capture_t;

// Sets up ADC calibration (first time only), configures the continuous ADC with
// the 4-step scan pattern at ADC_TOTAL_HZ and starts it. Returns false on failure.
bool analog_start(capture_t *c);

// Takes one block of results from the driver (waits up to ~20 ms) and sorts them:
// microphone values into c->mic[], temperature values into the running averages.
void analog_poll(capture_t *c);

// Stops the ADC, keeps the last partial temperature period if it is at least
// half full, and saves the overflow count in the capture.
void analog_stop(capture_t *c);
