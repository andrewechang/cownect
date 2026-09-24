#include "board_power.h"

#include "board_pins.h"
#include "cownect_config.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
constexpr const char* TAG = "POWER";
bool s_initialized = false;
bool s_commanded_on = false;
int64_t s_last_on_us = 0;
}  // namespace

esp_err_t board_power_init()
{
    if (s_initialized) {
        return ESP_OK;
    }
    const auto pin = static_cast<gpio_num_t>(cownect::board::PIN_PERIPH_EN);
    // Set the output latch LOW before enabling the driver so the pin never glitches high.
    gpio_set_level(pin, 0);
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << cownect::board::PIN_PERIPH_EN;
    cfg.mode = GPIO_MODE_OUTPUT;
    cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;  // R2 100k pulldown is on the PCB
    cfg.intr_type = GPIO_INTR_DISABLE;
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        return err;
    }
    gpio_set_level(pin, 0);
    s_commanded_on = false;
    s_initialized = true;
    return ESP_OK;
}

esp_err_t board_peripherals_set_enabled(bool enabled)
{
    if (!s_initialized) {
        esp_err_t err = board_power_init();
        if (err != ESP_OK) {
            return err;
        }
    }
    esp_err_t err = gpio_set_level(static_cast<gpio_num_t>(cownect::board::PIN_PERIPH_EN), enabled ? 1 : 0);
    if (err != ESP_OK) {
        return err;
    }
    if (enabled && !s_commanded_on) {
        s_last_on_us = esp_timer_get_time();
    }
    s_commanded_on = enabled;
    ESP_LOGI(TAG, "peripheral command = %s (physical rail not measured; ECO may force ON)",
             enabled ? "ON" : "OFF");
    return ESP_OK;
}

bool board_peripherals_commanded_enabled()
{
    return s_commanded_on;
}

esp_err_t board_peripherals_wait_stabilization()
{
    if (!s_commanded_on) {
        return ESP_ERR_INVALID_STATE;
    }
    const int64_t required_us = static_cast<int64_t>(cownect::config::PERIPH_STABILIZE_MS) * 1000;
    const int64_t elapsed_us = esp_timer_get_time() - s_last_on_us;
    if (elapsed_us < required_us) {
        const int64_t remaining_ms = (required_us - elapsed_us + 999) / 1000;
        vTaskDelay(pdMS_TO_TICKS(remaining_ms));
    }
    return ESP_OK;
}
