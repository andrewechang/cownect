#pragma once

#include "esp_err.h"

// Operation-mode API (Material 4 section 5). Higher layers never read GPIO15 directly.
//   GPIO15 LOW  -> PROGRAMMING (stay awake, development console)
//   GPIO15 HIGH -> NORMAL      (scheduler, timer deep sleep allowed)
//   bouncing    -> UNSTABLE    (never deep sleep)

enum class OperationMode {
    PROGRAMMING,
    NORMAL,
    UNSTABLE
};

esp_err_t board_mode_init();

// Debounced read: samples GPIO15 across the configured debounce window. Returns UNSTABLE if
// the level changed during the window. Blocks for about the debounce window.
OperationMode board_get_operation_mode();

const char* operation_mode_name(OperationMode mode);
