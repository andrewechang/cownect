// summary.c - simple statistics over the capture.
#include "summary.h"
#include <math.h>
#include <string.h>
#include "esp_log.h"

static const char *TAG = "summary";

// Accelerometer scale for the range chosen in accel.h (mg per digit -> g per digit)
#define ACCEL_G_PER_DIGIT (ACCEL_MG_PER_DIGIT / 1000.0f)

// Averages the valid temperature samples into *out. Returns false if none are valid.
static bool mean_temp(const temp_sample_t *t, uint32_t n, float *out)
{
    float sum = 0;
    uint32_t used = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (t[i].valid) { sum += t[i].temp_c; used++; }
    }
    if (used == 0) return false;
    *out = sum / used;
    return true;
}

// Fills the summary from the capture:
//   GPS          -> last valid fix
//   temperatures -> mean of the valid samples
//   accelerometer-> magnitude sqrt(x^2+y^2+z^2) in g, then its mean and RMS
//   microphone   -> RMS after removing the average (DC) level
//   flags        -> SUM_* bits for anything missing or degraded
// Also logs the result.
void summary_compute(const capture_t *c, bool last_cycle_overrun, summary_t *s)
{
    memset(s, 0, sizeof(*s));
    uint32_t cap = capture_status_flags(c);

    // GPS: last valid fix
    for (uint32_t i = 0; i < c->gps_count; i++) {
        if (c->gps[i].valid) {
            s->gps_valid = true;
            s->lat_e7 = c->gps[i].lat_e7;
            s->lon_e7 = c->gps[i].lon_e7;
        }
    }

    s->board_valid = mean_temp(c->board_temp, c->board_temp_count, &s->board_temp_c);
    s->cow_valid = mean_temp(c->cow_temp, c->cow_temp_count, &s->cow_temp_c);

    // Accelerometer: magnitude of each sample in g
    if (c->accel_count > 0) {
        double sum = 0, sum_sq = 0;
        for (uint32_t i = 0; i < c->accel_count; i++) {
            float x = (c->accel[i].x >> 2) * ACCEL_G_PER_DIGIT;   // drop the 2 unused low bits
            float y = (c->accel[i].y >> 2) * ACCEL_G_PER_DIGIT;
            float z = (c->accel[i].z >> 2) * ACCEL_G_PER_DIGIT;
            float mag = sqrtf(x * x + y * y + z * z);
            sum += mag;
            sum_sq += (double)mag * mag;
        }
        s->accel_valid = true;
        s->accel_mean_g = (float)(sum / c->accel_count);
        s->accel_rms_g = (float)sqrt(sum_sq / c->accel_count);
    }

    // Microphone: remove the DC level, then RMS
    if (c->mic_count > 0) {
        double sum = 0, sum_sq = 0;
        for (uint32_t i = 0; i < c->mic_count; i++) {
            sum += c->mic[i];
            sum_sq += (double)c->mic[i] * c->mic[i];
        }
        double mean = sum / c->mic_count;
        double var = sum_sq / c->mic_count - mean * mean;
        s->mic_valid = true;
        s->mic_rms = (float)sqrt(var > 0 ? var : 0);
    }

    uint32_t f = 0;
    if (cap & (CAP_ACCEL_DEGRADED | CAP_MIC_DEGRADED | CAP_GPS_DEGRADED)) f |= SUM_CAPTURE_DEGRADED;
    if (!s->accel_valid) f |= SUM_ACCEL_ERROR;
    if (!s->gps_valid) f |= SUM_GPS_NO_FIX;
    if (!s->mic_valid) f |= SUM_MIC_ERROR;
    if (!s->board_valid) f |= SUM_BOARD_TEMP_ERROR;
    if (!s->cow_valid) f |= SUM_COW_TEMP_ERROR;
    if (cap & CAP_COW_PROBE_FAULT) f |= SUM_COW_PROBE_FAULT;
    if (!(cap & CAP_ADC_OK)) f |= SUM_ADC_ERROR;
    if (last_cycle_overrun) f |= SUM_CYCLE_OVERRUN;
    s->status_flags = f;

    ESP_LOGI(TAG, "board %.2f C%s | cow %.2f C%s | accel mean %.3f g rms %.3f g | mic rms %.1f | flags 0x%03X",
             s->board_temp_c, s->board_valid ? "" : " (invalid)", s->cow_temp_c, s->cow_valid ? "" : " (invalid)",
             s->accel_mean_g, s->accel_rms_g, s->mic_rms, (unsigned)f);
    if (s->gps_valid) ESP_LOGI(TAG, "position %.7f, %.7f", s->lat_e7 / 1e7, s->lon_e7 / 1e7);
}
