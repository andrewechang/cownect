// features.c - see features.h.
#include "features.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "features";

#define WINDOW_MS   100                                   // sound-event window
#define MAX_WINDOWS (FEATURE_MAX_SECONDS * 1000 / WINDOW_MS)

// RMS (ADC codes) -> loudness code, see features.h.
static uint8_t db_code(double sum_sq, uint32_t n)
{
    if (n == 0) return 0;
    float rms = sqrtf((float)(sum_sq / n));
    if (rms <= 1.0f) return 0;
    float code = 60.0f * log10f(rms);
    return code > 255 ? 255 : (uint8_t)(code + 0.5f);
}

// ---------------------------------------------------------------- accelerometer
static void accel_features(const capture_t *c, features_t *f)
{
    const uint32_t per_block = ACCEL_RATE_HZ * FEATURE_BLOCK_MS / 1000;
    const float mg = ACCEL_MG_PER_DIGIT;
    for (uint32_t b = 0; b < f->block_count; b++) {
        feature_block_t *blk = &f->blocks[b];
        uint32_t first = b * per_block;
        uint32_t last = (b + 1 == f->block_count) ? c->accel.count : (b + 1) * per_block;  // leftovers -> last block
        if (last > c->accel.count) last = c->accel.count;
        if (first >= last) continue;                      // no samples: accel_ok stays false
        uint32_t n = last - first;

        double sx = 0, sy = 0, sz = 0;
        for (uint32_t i = first; i < last; i++) {
            sx += (c->accel.samples[i].x >> 2) * mg;      // drop the 2 unused low bits
            sy += (c->accel.samples[i].y >> 2) * mg;
            sz += (c->accel.samples[i].z >> 2) * mg;
        }
        float mx = sx / n, my = sy / n, mz = sz / n;      // average = gravity = collar tilt

        double odba = 0, jerk[3] = {0};
        float peak = 0, px = 0, py = 0, pz = 0;
        bool in_burst = false;
        uint32_t moves = 0;
        for (uint32_t i = first; i < last; i++) {
            float x = (c->accel.samples[i].x >> 2) * mg;
            float y = (c->accel.samples[i].y >> 2) * mg;
            float z = (c->accel.samples[i].z >> 2) * mg;
            if (i > first) {                              // derivative: change since the previous sample
                jerk[0] += fabsf(x - px);
                jerk[1] += fabsf(y - py);
                jerk[2] += fabsf(z - pz);
            }
            px = x; py = y; pz = z;
            float dx = x - mx, dy = y - my, dz = z - mz;   // movement = acceleration minus gravity
            odba += fabsf(dx) + fabsf(dy) + fabsf(dz);
            float dev = sqrtf(dx * dx + dy * dy + dz * dz);
            if (dev > peak) peak = dev;
            if (!in_burst && dev > FEATURE_MOVE_START_MG) { in_burst = true; moves++; }
            else if (in_burst && dev < FEATURE_MOVE_END_MG) in_burst = false;
        }
        blk->accel_ok = true;
        blk->mean_mg[0] = (int16_t)lroundf(mx);
        blk->mean_mg[1] = (int16_t)lroundf(my);
        blk->mean_mg[2] = (int16_t)lroundf(mz);
        double o = odba / n;
        blk->odba_mg = o > 65535 ? 65535 : (uint16_t)(o + 0.5);
        blk->peak_mg = peak > 65535 ? 65535 : (uint16_t)(peak + 0.5f);
        blk->moves = moves > 255 ? 255 : (uint8_t)moves;
        for (int a = 0; a < 3; a++) {
            // average mg per sample step x samples per second = mg/s; / 1000 = g/s; x 100 for the packet
            double gps_x100 = n > 1 ? jerk[a] / (n - 1) * ACCEL_RATE_HZ / 10.0 : 0;
            blk->jerk_cgs[a] = gps_x100 > 65535 ? 65535 : (uint16_t)(gps_x100 + 0.5);
        }
    }
}

// ---------------------------------------------------------------- microphone
// Band-pass filter (RBJ biquad, 0 dB at the centre frequency) between lo and hi Hz.
typedef struct { float b0, b2, a1, a2, x1, x2, y1, y2; } biquad_t;

static void bandpass_init(biquad_t *q, float lo, float hi)
{
    float f0 = sqrtf(lo * hi), Q = f0 / (hi - lo);
    float w0 = 2.0f * (float)M_PI * f0 / MIC_SAMPLE_RATE_HZ;
    float alpha = sinf(w0) / (2.0f * Q), a0 = 1.0f + alpha;
    memset(q, 0, sizeof(*q));
    q->b0 = alpha / a0;
    q->b2 = -alpha / a0;
    q->a1 = -2.0f * cosf(w0) / a0;
    q->a2 = (1.0f - alpha) / a0;
}

static inline float bandpass(biquad_t *q, float x)
{
    float y = q->b0 * x + q->b2 * q->x2 - q->a1 * q->y1 - q->a2 * q->y2;
    q->x2 = q->x1; q->x1 = x;
    q->y2 = q->y1; q->y1 = y;
    return y;
}

static int compare_float(const void *a, const void *b)
{
    float x = *(const float *)a, y = *(const float *)b;
    return (x > y) - (x < y);
}

// One pass over the samples: DC removed by a high-pass (~25 Hz), then
//   per second    -> loudness
//   per block     -> loudness of the three bands
//   per 100 ms    -> loudness, used to count loud sound events
static void mic_features(const capture_t *c, features_t *f)
{
    const uint16_t *mic = c->analog.mic;
    const uint32_t n = c->analog.mic_count;
    if (n == 0 || mic == NULL) return;

    static const float BANDS[3][2] = {{FEATURE_BAND_LOW_HZ}, {FEATURE_BAND_MID_HZ}, {FEATURE_BAND_HIGH_HZ}};
    biquad_t band[3];
    for (int k = 0; k < 3; k++) bandpass_init(&band[k], BANDS[k][0], BANDS[k][1]);

    const uint32_t per_second = MIC_SAMPLE_RATE_HZ;
    const uint32_t per_block = (uint32_t)((uint64_t)MIC_SAMPLE_RATE_HZ * FEATURE_BLOCK_MS / 1000);
    const uint32_t per_window = MIC_SAMPLE_RATE_HZ * WINDOW_MS / 1000;

    // Per-sample sums are float (the ESP32-S3 has float hardware, double is done in software)
    // and only over one 100 ms window; each window is then added to the double totals.
    // Seconds and blocks are whole numbers of windows, so every total gets whole windows.
    double sec_sq = 0, blk_sq[3] = {0};
    float win_sq = 0, win_band[3] = {0};
    uint32_t sec_n = 0, blk_n = 0, win_n = 0, sec = 0, blk = 0, windows = 0;
    static float window_db[MAX_WINDOWS];

    float x_prev = mic[0], hp = 0;
    for (uint32_t i = 0; i < n; i++) {
        float x = mic[i];
        hp = x - x_prev + 0.995f * hp;                    // high-pass: removes the ~1.65 V bias
        x_prev = x;

        win_sq += hp * hp;
        for (int k = 0; k < 3; k++) { float y = bandpass(&band[k], hp); win_band[k] += y * y; }
        win_n++; sec_n++; blk_n++;

        if (win_n == per_window || i + 1 == n) {          // window full (or last sample): add it up
            sec_sq += win_sq;
            for (int k = 0; k < 3; k++) { blk_sq[k] += win_band[k]; win_band[k] = 0; }
            if (win_n == per_window && windows < MAX_WINDOWS)
                window_db[windows++] = 20.0f * log10f(sqrtf(win_sq / win_n) + 1.0f);
            win_sq = 0; win_n = 0;
        }
        if (sec_n == per_second) {
            if (sec < f->second_count) f->loudness[sec] = db_code(sec_sq, sec_n);
            sec++; sec_sq = 0; sec_n = 0;
        }
        bool block_end = (blk_n == per_block && blk + 1 < f->block_count) || i + 1 == n;
        if (block_end && blk < f->block_count) {
            f->blocks[blk].mic_ok = true;
            for (int k = 0; k < 3; k++) { f->blocks[blk].band[k] = db_code(blk_sq[k], blk_n); blk_sq[k] = 0; }
            blk++; blk_n = 0;
        }
    }

    // Loud sound events: windows well above the typical (median) loudness.
    if (windows > 0) {
        static float sorted[MAX_WINDOWS];
        memcpy(sorted, window_db, windows * sizeof(float));
        qsort(sorted, windows, sizeof(float), compare_float);
        float threshold = sorted[windows / 2] + FEATURE_EVENT_DB;
        uint32_t events = 0;
        bool loud = false;
        for (uint32_t w = 0; w < windows; w++) {
            if (window_db[w] > threshold && !loud) events++;
            loud = window_db[w] > threshold;
        }
        f->sound_events = events > 255 ? 255 : (uint8_t)events;
    }
}

// The settings must give a packet that fits in one LoRa packet (checked when compiling).
_Static_assert(CAPTURE_MS / FEATURE_BLOCK_MS >= 1 && CAPTURE_MS / FEATURE_BLOCK_MS <= FEATURE_MAX_BLOCKS,
               "CAPTURE_MS / FEATURE_BLOCK_MS must be 1..FEATURE_MAX_BLOCKS");
_Static_assert(CAPTURE_MS / 1000 <= FEATURE_MAX_SECONDS, "CAPTURE_MS must be at most FEATURE_MAX_SECONDS");

void features_compute(const capture_t *c, features_t *f)
{
    memset(f, 0, sizeof(*f));
    int64_t t0 = esp_timer_get_time();

    int64_t ms = (c->end_us - c->start_us) / 1000;
    f->capture_ms = ms > 65535 ? 65535 : (uint16_t)ms;
    f->block_count = CAPTURE_MS / FEATURE_BLOCK_MS;
    f->second_count = CAPTURE_MS / 1000;

    accel_features(c, f);
    mic_features(c, f);

    ESP_LOGI(TAG, "%u blocks of %d ms, %u loud sound events (computed in %lld ms)",
             f->block_count, FEATURE_BLOCK_MS, f->sound_events, (esp_timer_get_time() - t0) / 1000);
    for (uint32_t b = 0; b < f->block_count; b++) {
        const feature_block_t *k = &f->blocks[b];
        ESP_LOGI(TAG, "  block %u: tilt %d/%d/%d mg | ODBA %u mg | peak %u mg | moves %u | "
                 "jerk %.2f/%.2f/%.2f g/s | sound low/mid/high %.1f/%.1f/%.1f dB", (unsigned)b,
                 k->mean_mg[0], k->mean_mg[1], k->mean_mg[2], k->odba_mg, k->peak_mg, k->moves,
                 k->jerk_cgs[0] / 100.0, k->jerk_cgs[1] / 100.0, k->jerk_cgs[2] / 100.0,
                 k->band[0] / 3.0, k->band[1] / 3.0, k->band[2] / 3.0);
    }
}
