#include "power_manager.h"

#include "board_power.h"

namespace cownect::power {

PowerManager& power_manager()
{
    static PowerManager instance;
    return instance;
}

esp_err_t PowerManager::peripherals_on()
{
    esp_err_t err = board_peripherals_set_enabled(true);
    if (err != ESP_OK) {
        return err;
    }
    return board_peripherals_wait_stabilization();
}

esp_err_t PowerManager::peripherals_off()
{
    return board_peripherals_set_enabled(false);
}

bool PowerManager::peripherals_commanded_on() const
{
    return board_peripherals_commanded_enabled();
}

}  // namespace cownect::power
