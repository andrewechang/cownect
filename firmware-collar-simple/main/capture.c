// capture.c - runs all sensors together for CAPTURE_MS in one simple loop.
//
//   loop until the capture time has passed:
//     read the next block of ADC data  (microphone + temperatures, ~4 ms per block)
//     every ACCEL_POLL_MS: empty the accelerometer FIFO
//     read any GPS text that has arrived
#include "capture.h"
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "capture";

// Clears the capture and reserves MIC_MAX_SAMPLES x 2 bytes of PSRAM for the
// microphone. Called once after boot; the buffer is reused by every capture.
bool capture_init(capture_t *c)
{
    memset(c, 0, sizeof(*c));
    c->mic = heap_caps_malloc(MIC_MAX_SAMPLES * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    if (!c->mic) {
        ESP_LOGE(TAG, "could not allocate %u bytes of PSRAM for the microphone",
                 (unsigned)(MIC_MAX_SAMPLES * sizeof(uint16_t)));
        return false;
    }
    return true;
}

// 1. clears the previous capture (keeps the PSRAM buffer)
// 2. starts accelerometer, GPS and ADC (a sensor that fails to start is skipped)
// 3. loops for CAPTURE_MS: ADC every pass, accelerometer every ACCEL_POLL_MS, GPS every pass
// 4. stops all sensors and logs how many samples each one produced
void capture_run(capture_t *c, uint32_t capture_id)
{
    uint16_t *mic = c->mic;
    memset(c, 0, sizeof(*c));
    c->mic = mic;
    c->capture_id = capture_id;

    c->accel_ok = accel_start();
    bool gps_ok = gps_start();
    c->adc_ok = analog_start(c);

    c->start_us = esp_timer_get_time();
    int64_t next_accel_us = c->start_us;
    int64_t now;
    while ((now = esp_timer_get_time()) - c->start_us < (int64_t)CAPTURE_MS * 1000) {
        if (c->adc_ok) analog_poll(c);          // also paces the loop (waits for data)
        else vTaskDelay(pdMS_TO_TICKS(10));

        if (c->accel_ok && now >= next_accel_us) {
            accel_poll(c);
            next_accel_us += (int64_t)ACCEL_POLL_MS * 1000;
        }
        if (gps_ok) gps_poll(c, (uint32_t)((now - c->start_us) / 1000));
    }
    c->end_us = esp_timer_get_time();

    if (c->adc_ok) analog_stop(c);
    if (gps_ok) gps_stop();
    if (c->accel_ok) accel_stop();

    uint32_t valid_fixes = 0;
    for (uint32_t i = 0; i < c->gps_count; i++) valid_fixes += c->gps[i].valid;
    ESP_LOGI(TAG, "capture %u done in %lld ms", (unsigned)capture_id, (c->end_us - c->start_us) / 1000);
    ESP_LOGI(TAG, "  accel %u samples (expected ~%u, overruns %u) | gps %u fixes, %u valid (bad sentences %u)",
             (unsigned)c->accel_count, (unsigned)(ACCEL_ODR_HZ * (CAPTURE_MS / 1000)), (unsigned)c->accel_overruns,
             (unsigned)c->gps_count, (unsigned)valid_fixes, (unsigned)c->gps_bad_sentences);
    ESP_LOGI(TAG, "  mic %u samples (expected ~%u, ADC overflows %u) | board temp %u | cow temp %u",
             (unsigned)c->mic_count, (unsigned)(MIC_SAMPLE_RATE_HZ * (CAPTURE_MS / 1000)),
             (unsigned)c->adc_overflows, (unsigned)c->board_temp_count, (unsigned)c->cow_temp_count);
}

// Returns true if at least one of the n temperature samples is valid.
static bool any_valid(const temp_sample_t *t, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) if (t[i].valid) return true;
    return false;
}

// Builds the CAP_* status bits: which sensors produced data, and which lost
// samples (FIFO overrun, ADC overflow, bad GPS sentences).
uint32_t capture_status_flags(const capture_t *c)
{
    uint32_t f = 0;
    if (c->accel_ok && c->accel_count > 0) f |= CAP_ACCEL_OK;
    if (c->gps_uart_ok) f |= CAP_GPS_UART_OK;
    for (uint32_t i = 0; i < c->gps_count; i++) if (c->gps[i].valid) { f |= CAP_GPS_HAD_FIX; break; }
    if (c->adc_ok && c->mic_count > 0) f |= CAP_MIC_OK;
    if (any_valid(c->board_temp, c->board_temp_count)) f |= CAP_BOARD_TEMP_OK;
    if (any_valid(c->cow_temp, c->cow_temp_count)) f |= CAP_COW_TEMP_OK;
    else if (c->cow_temp_count > 0) f |= CAP_COW_PROBE_FAULT;   // readings exist but none valid
    if (c->adc_ok) f |= CAP_ADC_OK;
    if (c->accel_overruns > 0) f |= CAP_ACCEL_DEGRADED;
    if (c->adc_overflows > 0) f |= CAP_MIC_DEGRADED;
    if (c->gps_bad_sentences > 0) f |= CAP_GPS_DEGRADED;
    return f;
}
