// main.c - CowNect collar, simple firmware version 2.
//
// Every CYCLE_MS (120 s):
//   1. sensor power on
//   2. record all sensors for CAPTURE_MS (30 s)          capture.c
//   3. check the data and make a summary                 capture.c
//   4. send:  COMM_HYBRID    -> summary over LoRa        lora.c
//                               + recording over Wi-Fi   wifi_upload.c
//             COMM_LORA_FULL -> recording over LoRa      lora_full.c
//   5. sensor power off, deep sleep until the next cycle
// After deep sleep the chip starts again at app_main().
#include <stdbool.h>
#include "config.h"
#include "capture.h"
#include "data_format.h"
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

// Kept in RTC memory: survives deep sleep (not a power loss).
RTC_DATA_ATTR static uint32_t rtc_magic;
RTC_DATA_ATTR static uint32_t next_capture_id;
RTC_DATA_ATTR static bool last_cycle_overrun;

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

// One cycle: power on -> record -> summary -> send -> power off.
static void run_cycle(int64_t cycle_start)
{
    uint32_t id = next_capture_id++;
    ESP_LOGI(TAG, "===== cycle %u =====", (unsigned)id);

    sensor_power(true);
    capture_run(&capture, id);

    summary_t summary;
    capture_summary(&capture, last_cycle_overrun, &summary);

#if COMM_MODE == COMM_LORA_FULL
    lora_full_send(&capture);                 // the radio needs the sensor power, so send first
    sensor_power(false);
#else
    uint8_t packet[TELEMETRY_BYTES];
    lora_send(packet, build_telemetry(&capture, &summary, packet));
    sensor_power(false);                      // Wi-Fi is inside the ESP32, no sensor power needed
    wifi_upload_capture(&capture);
#endif

    int64_t awake_ms = (esp_timer_get_time() - cycle_start) / 1000;
    last_cycle_overrun = awake_ms >= CYCLE_MS;
    ESP_LOGI(TAG, "cycle %u took %lld ms%s", (unsigned)id, awake_ms,
             last_cycle_overrun ? "  <- OVERRUN (longer than the cycle)" : "");
}

// Start-up (also after every deep sleep):
//   first power-up only -> reset the counters, check the LoRa wiring
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

    if (rtc_magic != 0xC0C0CAFE) {                  // first power-up, not a wake from sleep
        rtc_magic = 0xC0C0CAFE;
        next_capture_id = 1;
        last_cycle_overrun = false;
        ESP_LOGI(TAG, "CowNect simple firmware v2, device %u", (unsigned)DEVICE_ID);
        sensor_power(true);
        lora_spi_check();                           // wiring check only, no transmission
        sensor_power(false);
    }

    while (!normal_mode()) {
        ESP_LOGI(TAG, "PROGRAMMING mode (GPIO15 LOW): idle");
        vTaskDelay(pdMS_TO_TICKS(5000));
        cycle_start = esp_timer_get_time();
    }

    if (!capture_init(&capture)) return;            // no PSRAM: cannot record the microphone

    while (true) {
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
