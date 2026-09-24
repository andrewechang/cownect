#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

// Board I2C bus on GPIO6 (SDA) / GPIO7 (SCL). External 4.7k pull-ups (R3/R19) are on the PCB,
// so internal pull-ups stay disabled. The bus persists across SWITCHED_3V3 power cycles.

esp_err_t board_i2c_init();
i2c_master_bus_handle_t board_i2c_bus();  // nullptr until board_i2c_init() succeeds
