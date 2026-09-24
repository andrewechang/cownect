#include "board_mode.h"

#include "board_pins.h"
#include "cownect_config.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
constexpr uint32_t kSamplePeriodMs = 5;
bool s_initialized = false;
}  // namespace

esp_err_t board_mode_init()
{
    if (s_initialized) {
        return ESP_OK;
    }
    // R22 is a series resistor from the PROG switch common; the switch drives the net to GND or
    // ALWAYS_ON_3V3. No internal pull is enabled (not validated); a floating read during the
    // break-before-make transition is caught by the debounce and reported as UNSTABLE.
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << cownect::board::PIN_MODE_SW;
    cfg.mode = GPIO_MODE_INPUT;
    cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    esp_err_t err = gpio_config(&cfg);
    if (err == ESP_OK) {
        s_initialized = true;
    }
    return err;
}

OperationMode board_get_operation_mode()
{
    if (!s_initialized && board_mode_init() != ESP_OK) {
        return OperationMode::UNSTABLE;
    }
    const uint32_t samples = cownect::config::MODE_DEBOUNCE_MS / kSamplePeriodMs + 1;
    const int first = gpio_get_level(static_cast<gpio_num_t>(cownect::board::PIN_MODE_SW));
    for (uint32_t i = 1; i < samples; ++i) {
        vTaskDelay(pdMS_TO_TICKS(kSamplePeriodMs));
        if (gpio_get_level(static_cast<gpio_num_t>(cownect::board::PIN_MODE_SW)) != first) {
            return OperationMode::UNSTABLE;
        }
    }
    return first ? OperationMode::NORMAL : OperationMode::PROGRAMMING;
}

const char* operation_mode_name(OperationMode mode)
{
    switch (mode) {
    case OperationMode::PROGRAMMING: return "PROGRAMMING";
    case OperationMode::NORMAL:      return "NORMAL";
    case OperationMode::UNSTABLE:    return "UNSTABLE";
    }
    return "?";
}
