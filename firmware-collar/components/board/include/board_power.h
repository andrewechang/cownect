#pragma once

#include "esp_err.h"

// Switched peripheral rail command (Material 4 section 6).
//
// GPIO4 HIGH requests TPS22919 ON, LOW requests OFF. The ECO switch can force the rail ON,
// so these functions report only the COMMAND, never the physical rail state.
// Only the power manager / scheduler / explicit development tests should call the setter.

esp_err_t board_power_init();  // configures GPIO4 as output, commanded OFF

esp_err_t board_peripherals_set_enabled(bool enabled);
bool board_peripherals_commanded_enabled();

// Blocks until the configured stabilization delay (DEVELOPMENT DEFAULT) has elapsed since the
// last OFF->ON command. Returns immediately if already elapsed. Returns ESP_ERR_INVALID_STATE
// if the rail is currently commanded OFF.
esp_err_t board_peripherals_wait_stabilization();
