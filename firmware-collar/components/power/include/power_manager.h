#pragma once

#include "esp_err.h"

namespace cownect::power {

// Owner of the SWITCHED_3V3 command (Material 10 section 54). Reports the COMMAND only:
// ECO can force the rail on, and no rail-measurement input exists (no "measured" field).
class PowerManager {
public:
    esp_err_t peripherals_on();   // command ON + centralized stabilization delay
    esp_err_t peripherals_off();  // command OFF
    bool peripherals_commanded_on() const;
};

PowerManager& power_manager();

}  // namespace cownect::power
