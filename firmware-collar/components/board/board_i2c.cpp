#include "board_i2c.h"

#include "board_pins.h"

namespace {
i2c_master_bus_handle_t s_bus = nullptr;
}

esp_err_t board_i2c_init()
{
    if (s_bus != nullptr) {
        return ESP_OK;
    }
    i2c_master_bus_config_t cfg = {};
    cfg.i2c_port = I2C_NUM_0;
    cfg.sda_io_num = static_cast<gpio_num_t>(cownect::board::PIN_I2C_SDA);
    cfg.scl_io_num = static_cast<gpio_num_t>(cownect::board::PIN_I2C_SCL);
    cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    cfg.glitch_ignore_cnt = 7;
    cfg.flags.enable_internal_pullup = false;
    return i2c_new_master_bus(&cfg, &s_bus);
}

i2c_master_bus_handle_t board_i2c_bus()
{
    return s_bus;
}
