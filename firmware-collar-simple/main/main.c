// main.c - CowNect collar, simple prototype firmware.
//
// One 120 s cycle:
//   1. switch the sensor power rail on
//   2. record all sensors for 30 s                     (capture.c)
//   3. compute a small summary                         (summary.c)
//   4. send the summary over LoRa                      (lora.c)
//   5. send the raw recording over Wi-Fi to the Jetson (wifi_upload.c)
//      (or, with COMM_MODE = COMM_LORA_FULL, steps 4+5 are replaced by sending the
//       whole raw recording over LoRa in fragments   (lora_full.c))
//   6. switch the rail off and deep sleep for the rest of the 120 s
// After deep sleep the chip restarts from app_main(), so the cycle repeats.
#include <stdbool.h>
#include "config.h"
#include "capture.h"
#include "summary.h"
#include "packets.h"
#include "lora.h"
#include "lora_full.h"
#include "wifi_upload.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "main";

// RTC memory survives deep sleep (but not a power loss).
#define RTC_MAGIC 0xC0C0CAFE
RTC_DATA_ATTR static uint32_t s_rtc_magic;
RTC_DATA_ATTR static uint32_t s_next_capture_id;
RTC_DATA_ATTR static bool s_last_cycle_overrun;

static capture_t s_capture;     // ~13 KB, plus the 1.9 MB microphone buffer in PSRAM

// Switches the sensor power rail (PERIPH_EN). After switching on, waits
// RAIL_STABILIZE_MS so the sensors and radio are ready.
static void rail_set(bool on)
{
    gpio_set_level(PIN_PERIPH_EN, on ? 1 : 0);
    if (on) vTaskDelay(pdMS_TO_TICKS(RAIL_STABILIZE_MS));
}

// Reads the PROG switch. Returns true (NORMAL) only if GPIO15 reads HIGH five
// times in a row over 50 ms; any LOW reading means PROGRAMMING.
static bool normal_mode(void)
{
    for (int i = 0; i < 5; i++) {
        if (gpio_get_level(PIN_MODE_SW) == 0) return false;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return true;
}

// Sets PERIPH_EN as an output (rail off) and the PROG switch as an input.
static void pins_init(void)
{
    gpio_config_t rail = { .pin_bit_mask = 1ULL << PIN_PERIPH_EN, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&rail);
    gpio_set_level(PIN_PERIPH_EN, 0);
    gpio_config_t mode = { .pin_bit_mask = 1ULL << PIN_MODE_SW, .mode = GPIO_MODE_INPUT };  // switch drives both levels
    gpio_config(&mode);
}

// One cycle: rail on -> 30 s capture -> summary -> send (HYBRID or LORA_FULL,
// see config.h) -> rail off. Records whether the cycle took longer than CYCLE_MS.
static void run_one_cycle(int64_t cycle_start_us)
{
    uint32_t id = s_next_capture_id++;
    ESP_LOGI(TAG, "===== cycle start, capture %u =====", (unsigned)id);

    rail_set(true);
    capture_run(&s_capture, id);

    summary_t sum;
    summary_compute(&s_capture, s_last_cycle_overrun, &sum);

#if COMM_MODE == COMM_LORA_FULL
    // Whole raw capture over LoRa (the radio is on the sensor rail, so the rail stays on).
    lora_full_send(&s_capture);   // skipped with a message until the lora.h / lora_full.h values are set
    rail_set(false);
#else
    uint8_t pkt[TELEMETRY_PACKET_BYTES];
    size_t len = packet_build_telemetry(&s_capture, &sum, pkt);
    lora_send(pkt, len);          // skipped with a message until the lora.h values are set

    rail_set(false);              // LoRa is done; Wi-Fi is inside the ESP32 and does not need the rail
    wifi_upload_capture(&s_capture);
#endif

    int64_t awake_ms = (esp_timer_get_time() - cycle_start_us) / 1000;
    s_last_cycle_overrun = awake_ms >= CYCLE_MS;
    ESP_LOGI(TAG, "cycle %u awake for %lld ms%s", (unsigned)id, awake_ms,
             s_last_cycle_overrun ? " (OVERRUN: longer than the cycle)" : "");
}

// Returns how long to sleep so the next cycle starts CYCLE_MS after this one
// started (0 if the cycle already took longer).
static uint64_t remaining_us(int64_t cycle_start_us)
{
    int64_t left = (int64_t)CYCLE_MS * 1000 - (esp_timer_get_time() - cycle_start_us);
    return left > 0 ? (uint64_t)left : 0;
}

// Program start (also after every wake-up from deep sleep):
//   first power-up only -> reset counters, LoRa SPI wiring check
//   PROG switch LOW     -> stay idle (safe to flash)
//   otherwise           -> run a cycle, then deep sleep (or wait, in bench mode)
void app_main(void)
{
    int64_t cycle_start = esp_timer_get_time();   // ~0 right after (re)boot
    pins_init();

    if (s_rtc_magic != RTC_MAGIC) {               // first power-up (not a wake from sleep)
        s_rtc_magic = RTC_MAGIC;
        s_next_capture_id = 1;
        s_last_cycle_overrun = false;
        ESP_LOGI(TAG, "CowNect simple firmware, device %u, power-on start", (unsigned)DEVICE_ID);
        rail_set(true);                           // the radio is powered from the sensor rail
        lora_spi_check();                         // wiring check only, no transmission
        rail_set(false);
    }

    // PROG switch LOW = programming: stay idle and awake so the board can be flashed.
    while (!normal_mode()) {
        ESP_LOGI(TAG, "PROGRAMMING mode (GPIO15 LOW): idle, nothing is running");
        vTaskDelay(pdMS_TO_TICKS(5000));
        cycle_start = esp_timer_get_time();
    }

    if (!capture_init(&s_capture)) {
        ESP_LOGE(TAG, "no PSRAM - cannot record the microphone. Stopping.");
        return;
    }

    while (true) {
        run_one_cycle(cycle_start);
        uint64_t sleep_us = remaining_us(cycle_start);

        if (DEBUG_NO_DEEP_SLEEP || !normal_mode()) {
            // Bench mode (or the switch was moved to PROG): wait awake instead of sleeping.
            ESP_LOGI(TAG, "waiting %llu ms (no deep sleep)", sleep_us / 1000);
            vTaskDelay(pdMS_TO_TICKS(sleep_us / 1000));
            while (!normal_mode()) vTaskDelay(pdMS_TO_TICKS(1000));
            cycle_start = esp_timer_get_time();
            continue;
        }

        ESP_LOGI(TAG, "deep sleep for %llu ms", sleep_us / 1000);
        esp_sleep_enable_timer_wakeup(sleep_us > 0 ? sleep_us : 1000);   // overrun: wake almost at once
        esp_deep_sleep_start();                   // does not return; app_main runs again on wake
    }
}
