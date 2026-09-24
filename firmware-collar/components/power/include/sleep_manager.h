#pragma once

#include <cstdint>
#include "esp_err.h"
#include "esp_sleep.h"
#include "esp_system.h"

namespace cownect::power {

// Timer-only deep sleep (Material 10 sections 14, 27). No GPIO/touch/ULP/USB wake source.
class SleepManager {
public:
    esp_err_t configure_timer_wakeup(uint64_t duration_us);
    // Commands PERIPH_EN low, flushes logs briefly and enters deep sleep. Does not return.
    [[noreturn]] void enter_deep_sleep();
};

SleepManager& sleep_manager();

const char* wake_cause_name(esp_sleep_wakeup_cause_t cause);
const char* reset_reason_name(esp_reset_reason_t reason);

}  // namespace cownect::power
