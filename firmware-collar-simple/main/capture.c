// capture.c - records all sensors at the same time, then checks and summarises the data.
#include "capture.h"
#include <math.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "capture";

// Reserves the microphone buffer once; every capture reuses it.
bool capture_init(capture_t *c)
{
    memset(c, 0, sizeof(*c));
    return analog_init(&c->analog);
}

// Clears the old data, starts the sensors, then loops for CAPTURE_MS:
//   every pass:               read the next ADC block (this also paces the loop) + GPS text
//   every ACCEL_READ_EVERY_MS: empty the accelerometer FIFO
// Finally stops the sensors and prints how much data each one produced.
void capture_run(capture_t *c, uint32_t capture_id)
{
    uint16_t *mic_buffer = c->analog.mic;       // keep the PSRAM buffer
    memset(c, 0, sizeof(*c));
    c->analog.mic = mic_buffer;
    c->capture_id = capture_id;

    c->accel.ok = accel_start();
    bool gps_on = gps_start();
    c->analog.ok = analog_start(&c->analog);

    c->start_us = esp_timer_get_time();
    int64_t next_accel = c->start_us;
    int64_t now;
    while ((now = esp_timer_get_time()) - c->start_us < (int64_t)CAPTURE_MS * 1000) {
        if (c->analog.ok) analog_read(&c->analog);
        else vTaskDelay(pdMS_TO_TICKS(10));

        if (c->accel.ok && now >= next_accel) {
            accel_read_fifo(&c->accel);
            next_accel += (int64_t)ACCEL_READ_EVERY_MS * 1000;
        }
        if (gps_on) gps_read(&c->gps, (uint32_t)((now - c->start_us) / 1000));
    }
    c->end_us = esp_timer_get_time();

    if (c->analog.ok) analog_stop(&c->analog);
    if (gps_on) gps_stop();
    if (c->accel.ok) accel_stop();

    uint32_t valid_fixes = 0;
    for (uint32_t i = 0; i < c->gps.count; i++) valid_fixes += c->gps.fixes[i].valid;
    ESP_LOGI(TAG, "capture %u done in %lld ms", (unsigned)capture_id, (c->end_us - c->start_us) / 1000);
    ESP_LOGI(TAG, "  accel: %u samples (expected ~%u), FIFO overruns %u",
             (unsigned)c->accel.count, (unsigned)(ACCEL_RATE_HZ * (CAPTURE_MS / 1000)), (unsigned)c->accel.overruns);
    ESP_LOGI(TAG, "  gps:   %u fixes, %u valid, bad sentences %u%s", (unsigned)c->gps.count, (unsigned)valid_fixes,
             (unsigned)c->gps.bad_sentences, c->gps.uart_ok ? "" : "  <- NO GPS TEXT (wiring/power?)");
    ESP_LOGI(TAG, "  mic:   %u samples (expected ~%u), ADC overflows %u, clipped %u (%.3f%%)",
             (unsigned)c->analog.mic_count, (unsigned)(MIC_SAMPLE_RATE_HZ * (CAPTURE_MS / 1000)),
             (unsigned)c->analog.overflows, (unsigned)c->analog.mic_clipped,
             c->analog.mic_count ? 100.0 * c->analog.mic_clipped / c->analog.mic_count : 0.0);
    ESP_LOGI(TAG, "  temps: board %u values, cow %u values", (unsigned)c->analog.board_count, (unsigned)c->analog.cow_count);
}

// Returns true if any of the n temperature values is valid.
static bool any_valid(const temp_sample_t *t, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) if (t[i].valid) return true;
    return false;
}

// Builds the CAP_* flags: what worked, and what lost or damaged data.
uint32_t capture_flags(const capture_t *c)
{
    uint32_t f = 0;
    if (c->accel.ok && c->accel.count > 0) f |= CAP_ACCEL_OK;
    if (c->gps.uart_ok) f |= CAP_GPS_UART_OK;
    for (uint32_t i = 0; i < c->gps.count; i++) if (c->gps.fixes[i].valid) f |= CAP_GPS_HAD_FIX;
    if (c->analog.ok && c->analog.mic_count > 0) f |= CAP_MIC_OK;
    if (any_valid(c->analog.board, c->analog.board_count)) f |= CAP_BOARD_TEMP_OK;
    if (any_valid(c->analog.cow, c->analog.cow_count)) f |= CAP_COW_TEMP_OK;
    else if (c->analog.cow_count > 0) f |= CAP_COW_PROBE_FAULT;
    if (c->analog.ok) f |= CAP_ADC_OK;
    if (c->accel.overruns > 0) f |= CAP_ACCEL_LOST;
    if (c->analog.overflows > 0) f |= CAP_MIC_LOST;
    if (c->gps.bad_sentences > 0) f |= CAP_GPS_BAD_TEXT;
    if (c->analog.mic_clipped > 0) f |= CAP_MIC_CLIPPED;
    return f;
}

// Average of the valid temperatures. False if none are valid.
static bool average_temp(const temp_sample_t *t, uint32_t n, float *out)
{
    float sum = 0;
    uint32_t used = 0;
    for (uint32_t i = 0; i < n; i++) if (t[i].valid) { sum += t[i].temp_c; used++; }
    if (used == 0) return false;
    *out = sum / used;
    return true;
}

// Computes the numbers sent in the LoRa telemetry:
//   GPS -> last valid position; temperatures -> averages;
//   accelerometer -> mean and RMS of sqrt(x^2+y^2+z^2) in g;
//   microphone -> RMS with the average (DC) level removed; flags -> SUM_* bits.
void capture_summary(const capture_t *c, bool last_cycle_overrun, summary_t *s)
{
    memset(s, 0, sizeof(*s));

    for (uint32_t i = 0; i < c->gps.count; i++) {
        if (c->gps.fixes[i].valid) {
            s->gps_valid = true;
            s->lat_e7 = c->gps.fixes[i].lat_e7;
            s->lon_e7 = c->gps.fixes[i].lon_e7;
        }
    }
    s->board_valid = average_temp(c->analog.board, c->analog.board_count, &s->board_temp_c);
    s->cow_valid = average_temp(c->analog.cow, c->analog.cow_count, &s->cow_temp_c);

    if (c->accel.count > 0) {
        const float g_per_digit = ACCEL_MG_PER_DIGIT / 1000.0f;
        double sum = 0, sum_sq = 0;
        for (uint32_t i = 0; i < c->accel.count; i++) {
            float x = (c->accel.samples[i].x >> 2) * g_per_digit;   // drop the 2 unused low bits
            float y = (c->accel.samples[i].y >> 2) * g_per_digit;
            float z = (c->accel.samples[i].z >> 2) * g_per_digit;
            float mag = sqrtf(x * x + y * y + z * z);
            sum += mag;
            sum_sq += (double)mag * mag;
        }
        s->accel_valid = true;
        s->accel_mean_g = (float)(sum / c->accel.count);
        s->accel_rms_g = (float)sqrt(sum_sq / c->accel.count);
    }

    if (c->analog.mic_count > 0) {
        double sum = 0, sum_sq = 0;
        for (uint32_t i = 0; i < c->analog.mic_count; i++) {
            sum += c->analog.mic[i];
            sum_sq += (double)c->analog.mic[i] * c->analog.mic[i];
        }
        double mean = sum / c->analog.mic_count;
        double variance = sum_sq / c->analog.mic_count - mean * mean;
        s->mic_valid = true;
        s->mic_rms = (float)sqrt(variance > 0 ? variance : 0);
    }

    uint32_t cap = capture_flags(c);
    if (cap & (CAP_ACCEL_LOST | CAP_MIC_LOST | CAP_GPS_BAD_TEXT)) s->flags |= SUM_SOMETHING_LOST;
    if (!s->accel_valid) s->flags |= SUM_ACCEL_ERROR;
    if (!s->gps_valid) s->flags |= SUM_GPS_NO_FIX;
    if (!s->mic_valid) s->flags |= SUM_MIC_ERROR;
    if (!s->board_valid) s->flags |= SUM_BOARD_TEMP_ERROR;
    if (!s->cow_valid) s->flags |= SUM_COW_TEMP_ERROR;
    if (cap & CAP_COW_PROBE_FAULT) s->flags |= SUM_COW_PROBE_FAULT;
    if (!(cap & CAP_ADC_OK)) s->flags |= SUM_ADC_ERROR;
    if (last_cycle_overrun) s->flags |= SUM_CYCLE_OVERRUN;
    if (cap & CAP_MIC_CLIPPED) s->flags |= SUM_MIC_CLIPPED;

    ESP_LOGI(TAG, "summary: board %.2f C | cow %.2f C | accel mean %.3f g | mic rms %.1f | flags 0x%03X",
             s->board_temp_c, s->cow_temp_c, s->accel_mean_g, s->mic_rms, (unsigned)s->flags);
}
