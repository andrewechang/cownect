// main.c - CowNect collar, simple firmware version 2.
//
// Every CYCLE_MS (60 s):
//   1. sensor power on
//   2. record all sensors for CAPTURE_MS (15 s)          capture.c
//   3. check the data and make a summary                 capture.c
//   4. send:  COMM_LORA_FULL -> one packet of values over LoRa   features.c + lora.c
//             COMM_HYBRID    -> summary over LoRa                lora.c
//                               + raw recording over Wi-Fi       wifi_upload.c
//   5. sensor power off, deep sleep until the next cycle
// After deep sleep the chip starts again at app_main().
// Before the first cycle (power-up / reset) the GPS stays on until its first fix; see config.h
// "GPS first fix". The same wait runs again after GPS_RECOVER_AFTER_CYCLES cycles without a fix.
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "capture.h"
#include "data_format.h"
#include "features.h"
#include "lora.h"
#include "wifi_upload.h"
#include "test_mode.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_sleep.h"
#include "sdkconfig.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// The N16R8 module has 8 MB OCTAL PSRAM. A quad-mode build aborts at boot
// ("quad_psram: PSRAM chip is not connected"), so refuse to build one.
#if !CONFIG_SPIRAM || !CONFIG_SPIRAM_MODE_OCT
#error "N16R8 needs CONFIG_SPIRAM=y and CONFIG_SPIRAM_MODE_OCT=y (see sdkconfig.defaults): delete sdkconfig and rebuild"
#endif

static const char *TAG = "main";

// Kept in RTC memory: survives deep sleep (not a power loss).
RTC_DATA_ATTR static uint32_t rtc_magic;
RTC_DATA_ATTR static uint32_t next_capture_id;
RTC_DATA_ATTR static bool last_cycle_overrun;
RTC_DATA_ATTR static uint32_t gps_wait_max_ms;           // > 0: wait for a GPS fix before the next cycle
RTC_DATA_ATTR static uint32_t cycles_without_fix;        // cycles in a row with no GPS fix

static capture_t capture;

// Sensor power on/off. After switching on, waits for the sensors to be ready.
static void sensor_power(bool on)
{
    gpio_set_level(PIN_PERIPH_EN, on);
    if (on) vTaskDelay(pdMS_TO_TICKS(RAIL_STABILIZE_MS));
}

// PROG switch: true (NORMAL) only if GPIO15 reads HIGH 5 times in 50 ms.
static bool normal_mode(void)
{
    for (int i = 0; i < 5; i++) {
        if (gpio_get_level(PIN_MODE_SW) == 0) return false;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return true;
}

// Sends one status text by LoRa: "CM", version 1, type 0x12, then the text (no '\0').
// The receiver prints it as {"type":"collar_status","text":"..."}.
static void lora_status(const char *fmt, ...)
{
    uint8_t pkt[4 + 100] = {0x43, 0x4D, 1, 0x12};
    int n = snprintf((char *)&pkt[4], sizeof(pkt) - 4, "collar %u: ", (unsigned)DEVICE_ID);
    va_list ap;
    va_start(ap, fmt);
    n += vsnprintf((char *)&pkt[4 + n], sizeof(pkt) - 4 - n, fmt, ap);
    va_end(ap);
    if (n > (int)sizeof(pkt) - 5) n = sizeof(pkt) - 5;
    lora_send(pkt, 4 + n);
}

// Keeps the GPS powered and reading until it has a fix (+ GPS_EXTRA_ON_MS so it stores all
// satellites), or `max_ms` without one. Sends "GPS waiting, not fix yet" every GPS_WAIT_STATUS_MS.
// Returns true if it got a fix.
static bool gps_wait_for_fix(uint32_t max_ms)
{
    ESP_LOGI(TAG, "GPS: waiting for a fix before the normal cycle (max %u s)", (unsigned)(max_ms / 1000));
    sensor_power(true);
    if (!gps_start()) {
        ESP_LOGE(TAG, "GPS: UART could not be started");
        sensor_power(false);
        return false;
    }
    gps_data_t *g = &capture.gps;                    // the capture buffer is free until the cycle
    memset(g, 0, sizeof(*g));
    int64_t start = esp_timer_get_time();
    uint32_t fix_ms = UINT32_MAX, next_status_ms = 0;
    uint8_t sats = 0;
    while (true) {
        uint32_t t = (uint32_t)((esp_timer_get_time() - start) / 1000);
        gps_read(g, t);
        for (uint32_t i = 0; i < g->count; i++) {
            sats = g->fixes[i].satellites;
            if (g->fixes[i].valid && fix_ms == UINT32_MAX) {
                fix_ms = t;
                ESP_LOGI(TAG, "GPS: FIX after %u s, %u satellites (%.6f, %.6f) - staying on %u s",
                         (unsigned)(t / 1000), (unsigned)sats, g->fixes[i].lat_e7 / 1e7,
                         g->fixes[i].lon_e7 / 1e7, (unsigned)(GPS_EXTRA_ON_MS / 1000));
                lora_status("GPS fix after %u s, sats %u, staying on %u s", (unsigned)(t / 1000),
                            (unsigned)sats, (unsigned)(GPS_EXTRA_ON_MS / 1000));
            }
        }
        g->count = 0;                                // only the newest messages matter here
        if (fix_ms != UINT32_MAX) {
            if (t >= fix_ms + GPS_EXTRA_ON_MS) break;
        } else if (t >= max_ms) {
            ESP_LOGW(TAG, "GPS: no fix after %u s - starting the normal cycle anyway", (unsigned)(t / 1000));
            lora_status("GPS no fix after %u s, starting normal cycle", (unsigned)(t / 1000));
            break;
        } else if (t >= 10000 && !g->uart_ok) {
            ESP_LOGE(TAG, "GPS: no NMEA from the module (RX GPIO%d) - not waiting", PIN_GPS_RX);
            lora_status("GPS not answering, starting normal cycle");
            break;
        } else if (t >= next_status_ms) {
            ESP_LOGI(TAG, "GPS: waiting, not fix yet | %u s | sats %u", (unsigned)(t / 1000), (unsigned)sats);
            lora_status("GPS waiting, not fix yet | %u s | sats %u", (unsigned)(t / 1000), (unsigned)sats);
            next_status_ms += GPS_WAIT_STATUS_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    gps_stop();
    sensor_power(false);
    return fix_ms != UINT32_MAX;
}

// One cycle: power on -> record -> summary -> send -> power off.
static void run_cycle(int64_t cycle_start)
{
    uint32_t id = next_capture_id++;
    ESP_LOGI(TAG, "===== cycle %u =====", (unsigned)id);

    sensor_power(true);
    capture_run(&capture, id);

    summary_t summary;
    capture_summary(&capture, last_cycle_overrun, &summary);

    // Too many cycles without a fix (saved satellite data too old, or the collar was under a
    // roof): wait for a fix again before the next cycle.
    if (summary.gps_valid) {
        cycles_without_fix = 0;
    } else if (++cycles_without_fix >= GPS_RECOVER_AFTER_CYCLES) {
        ESP_LOGW(TAG, "GPS: %u cycles without a fix - longer GPS wait before the next cycle",
                 (unsigned)cycles_without_fix);
        gps_wait_max_ms = GPS_RECOVER_MAX_MS;
        cycles_without_fix = 0;
    }

    int64_t t_send = esp_timer_get_time();
    ESP_LOGI(TAG, "[TIME] record %lld ms", (t_send - cycle_start) / 1000);
#if COMM_MODE == COMM_LORA_FULL
    static features_t features;
    features_compute(&capture, &features);
    uint8_t packet[FEATURES_MAX_BYTES];
    size_t len = build_features(&capture, &summary, &features, packet);
    int64_t t_tx = esp_timer_get_time();
    bool lora_ok = lora_send(packet, len);    // one packet, no ACK: a lost packet costs one cycle
    sensor_power(false);
    ESP_LOGI(TAG, "[TIME] features %lld ms | LoRa %u bytes %s: %lld ms", (t_tx - t_send) / 1000,
             (unsigned)len, lora_ok ? "OK" : "FAILED", (esp_timer_get_time() - t_tx) / 1000);
#else
    uint8_t packet[TELEMETRY_BYTES];
    bool lora_ok = lora_send(packet, build_telemetry(&capture, &summary, packet));
    int64_t t_wifi = esp_timer_get_time();
    sensor_power(false);                      // Wi-Fi is inside the ESP32, no sensor power needed
    bool wifi_ok = wifi_upload_capture(&capture);
    ESP_LOGI(TAG, "[TIME] LoRa summary %s: %lld ms | Wi-Fi upload %s: %lld ms",
             lora_ok ? "OK" : "FAILED", (t_wifi - t_send) / 1000,
             wifi_ok ? "OK" : "FAILED", (esp_timer_get_time() - t_wifi) / 1000);
#endif

    int64_t awake_ms = (esp_timer_get_time() - cycle_start) / 1000;
    last_cycle_overrun = awake_ms >= CYCLE_MS;
    ESP_LOGI(TAG, "cycle %u took %lld ms%s", (unsigned)id, awake_ms,
             last_cycle_overrun ? "  <- OVERRUN (longer than the cycle)" : "");
}

// Start-up (also after every deep sleep):
//   first power-up only -> reset the counters, check the LoRa wiring, then wait for the first
//                          GPS fix before the first cycle (GPS_FIRST_FIX_MAX_MS, config.h)
//   PROG switch LOW     -> stay idle so the board can be flashed
//   otherwise           -> run a cycle, then deep sleep (or wait, in bench mode)
void app_main(void)
{
    int64_t cycle_start = esp_timer_get_time();

    gpio_config_t out = { .pin_bit_mask = 1ULL << PIN_PERIPH_EN, .mode = GPIO_MODE_OUTPUT };
    gpio_config_t in = { .pin_bit_mask = 1ULL << PIN_MODE_SW, .mode = GPIO_MODE_INPUT };   // switch drives both levels
    gpio_config(&out);
    gpio_config(&in);
    sensor_power(false);

    // One line per boot, so every log shows why the chip restarted and whether PSRAM came up.
    ESP_LOGI(TAG, "[BOOT] reset: %s | PSRAM: %s %u KB | internal RAM free %u KB",
             reset_reason_text(esp_reset_reason()), esp_psram_is_initialized() ? "OK" : "NOT DETECTED",
             (unsigned)(esp_psram_is_initialized() ? esp_psram_get_size() / 1024 : 0),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));

    // Bring-up tests instead of the normal cycle (TEST_NUMBER in test_mode.h; 0 = off).
    if (TEST_NUMBER != 0) test_run(&capture);   // never returns

    if (rtc_magic != 0xC0C0CAFE) {                  // first power-up, not a wake from sleep
        rtc_magic = 0xC0C0CAFE;
        next_capture_id = 1;
        last_cycle_overrun = false;
        ESP_LOGI(TAG, "CowNect simple firmware v2, device %u", (unsigned)DEVICE_ID);
        sensor_power(true);
        lora_spi_check();                           // wiring check only, no transmission
        sensor_power(false);
        gps_wait_max_ms = GPS_FIRST_FIX_MAX_MS;     // the GPS may have no saved data: first fix first
        cycles_without_fix = 0;
    }

    while (!normal_mode()) {
        ESP_LOGI(TAG, "PROGRAMMING mode (GPIO15 LOW): idle");
        vTaskDelay(pdMS_TO_TICKS(5000));
        cycle_start = esp_timer_get_time();
    }

    if (!capture_init(&capture)) {                  // no microphone buffer at all (PSRAM + fallback failed)
        // Do not sit awake doing nothing (drains the battery): try again after one cycle.
        ESP_LOGE(TAG, "no microphone buffer - cannot record; deep sleep and retry");
        esp_sleep_enable_timer_wakeup((uint64_t)CYCLE_MS * 1000);
        esp_deep_sleep_start();
    }

    while (true) {
        if (gps_wait_max_ms > 0) {                  // power-up, or too many cycles without a fix
            gps_wait_for_fix(gps_wait_max_ms);
            gps_wait_max_ms = 0;
            cycles_without_fix = 0;
            cycle_start = esp_timer_get_time();     // the cycle timing starts after the wait
        }
        run_cycle(cycle_start);
        int64_t left_us = (int64_t)CYCLE_MS * 1000 - (esp_timer_get_time() - cycle_start);
        if (left_us < 0) left_us = 0;               // overrun: start the next cycle at once

        if (DEBUG_NO_DEEP_SLEEP || !normal_mode()) {
            ESP_LOGI(TAG, "waiting %lld ms (no deep sleep)", left_us / 1000);
            vTaskDelay(pdMS_TO_TICKS(left_us / 1000));
            while (!normal_mode()) vTaskDelay(pdMS_TO_TICKS(1000));
            cycle_start = esp_timer_get_time();
            continue;
        }
        ESP_LOGI(TAG, "deep sleep for %lld ms", left_us / 1000);
        esp_sleep_enable_timer_wakeup(left_us > 0 ? left_us : 1000);
        esp_deep_sleep_start();                     // does not return
    }
}
