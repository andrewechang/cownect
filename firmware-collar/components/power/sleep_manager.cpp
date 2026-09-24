#include "sleep_manager.h"

#include <cstdio>
#include "board_power.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace cownect::power {
namespace {
constexpr const char* TAG = "SLEEP";
}

SleepManager& sleep_manager()
{
    static SleepManager instance;
    return instance;
}

esp_err_t SleepManager::configure_timer_wakeup(uint64_t duration_us)
{
    if (duration_us == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return esp_sleep_enable_timer_wakeup(duration_us);
}

void SleepManager::enter_deep_sleep()
{
    // Only the validated PERIPH_EN state is set; other GPIO sleep states are not yet validated
    // (hardware_questions.md). R2 100k keeps PERIPH_EN low if the pin floats in sleep.
    board_peripherals_set_enabled(false);
    ESP_LOGI(TAG, "entering timer deep sleep");
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(20));  // let the console drain
    esp_deep_sleep_start();
}

const char* wake_cause_name(esp_sleep_wakeup_cause_t cause)
{
    switch (cause) {
    case ESP_SLEEP_WAKEUP_UNDEFINED: return "RESET/POWER_ON";
    case ESP_SLEEP_WAKEUP_TIMER:     return "TIMER";
    case ESP_SLEEP_WAKEUP_EXT0:      return "EXT0";
    case ESP_SLEEP_WAKEUP_EXT1:      return "EXT1";
    case ESP_SLEEP_WAKEUP_GPIO:      return "GPIO";
    default:                         return "OTHER";
    }
}

const char* reset_reason_name(esp_reset_reason_t reason)
{
    switch (reason) {
    case ESP_RST_POWERON:   return "POWERON";
    case ESP_RST_EXT:       return "EXTERNAL";
    case ESP_RST_SW:        return "SOFTWARE";
    case ESP_RST_PANIC:     return "PANIC";
    case ESP_RST_INT_WDT:   return "INT_WDT";
    case ESP_RST_TASK_WDT:  return "TASK_WDT";
    case ESP_RST_WDT:       return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";
    case ESP_RST_SDIO:      return "SDIO";
    case ESP_RST_USB:       return "USB";
    default:                return "OTHER";
    }
}

}  // namespace cownect::power
