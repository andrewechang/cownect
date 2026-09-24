#include "sx1262_hal_espidf.h"

#include <cstring>
#include "cownect_err.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sx126x_hal.h"

namespace cownect::radio {
namespace {

// SX1262 datasheet: NRESET must be held low for at least 100 us. 1 ms is used (margin).
constexpr uint32_t kResetLowUs = 1000;
constexpr uint32_t kSpinPhaseUs = 1000;  // short busy phases are polled, longer ones yield

void cs(const Sx1262HalContext& c, int level)
{
    gpio_set_level(static_cast<gpio_num_t>(c.pin_cs), level);
}

esp_err_t transfer(Sx1262HalContext& c, size_t len)
{
    spi_transaction_t t = {};
    t.length = len * 8;
    t.rxlength = len * 8;
    t.tx_buffer = c.dma_tx;
    t.rx_buffer = c.dma_rx;
    esp_err_t err = spi_device_polling_transmit(c.spi, &t);
    if (err != ESP_OK) {
        c.spi_error_count++;
        c.last_error = err;
    }
    return err;
}

}  // namespace

esp_err_t sx1262_wait_while_busy(Sx1262HalContext& c, uint32_t timeout_ms)
{
    const int64_t start = esp_timer_get_time();
    const int64_t deadline = start + static_cast<int64_t>(timeout_ms) * 1000;
    while (gpio_get_level(static_cast<gpio_num_t>(c.pin_busy)) != 0) {
        const int64_t now = esp_timer_get_time();
        if (now >= deadline) {
            c.busy_timeout_count++;
            c.last_error = COWNECT_ERR_BUSY_TIMEOUT;
            c.last_busy_wait_us = static_cast<uint32_t>(now - start);
            return COWNECT_ERR_BUSY_TIMEOUT;
        }
        if (now - start < kSpinPhaseUs) {
            esp_rom_delay_us(10);
        } else {
            vTaskDelay(1);
        }
    }
    c.last_busy_wait_us = static_cast<uint32_t>(esp_timer_get_time() - start);
    return ESP_OK;
}

esp_err_t sx1262_hardware_reset(Sx1262HalContext& c)
{
    const auto rst = static_cast<gpio_num_t>(c.pin_reset);
    gpio_set_level(rst, 0);
    esp_rom_delay_us(kResetLowUs);
    gpio_set_level(rst, 1);
    c.reset_count++;
    esp_rom_delay_us(100);  // allow BUSY to assert after reset release
    return sx1262_wait_while_busy(c, c.busy_timeout_ms);
}

}  // namespace cownect::radio

using cownect::radio::Sx1262HalContext;

extern "C" sx126x_hal_status_t sx126x_hal_write(const void* context, const uint8_t* command,
                                                const uint16_t command_length, const uint8_t* data,
                                                const uint16_t data_length)
{
    auto& c = *static_cast<Sx1262HalContext*>(const_cast<void*>(context));
    const size_t total = static_cast<size_t>(command_length) + data_length;
    if (total == 0 || total > c.dma_capacity) {
        return SX126X_HAL_STATUS_ERROR;
    }
    if (cownect::radio::sx1262_wait_while_busy(c, c.busy_timeout_ms) != ESP_OK) {
        return SX126X_HAL_STATUS_ERROR;
    }
    std::memcpy(c.dma_tx, command, command_length);
    if (data_length > 0) {
        std::memcpy(c.dma_tx + command_length, data, data_length);
    }
    cownect::radio::cs(c, 0);
    esp_err_t err = cownect::radio::transfer(c, total);
    cownect::radio::cs(c, 1);
    return err == ESP_OK ? SX126X_HAL_STATUS_OK : SX126X_HAL_STATUS_ERROR;
}

extern "C" sx126x_hal_status_t sx126x_hal_read(const void* context, const uint8_t* command,
                                               const uint16_t command_length, uint8_t* data,
                                               const uint16_t data_length)
{
    auto& c = *static_cast<Sx1262HalContext*>(const_cast<void*>(context));
    const size_t total = static_cast<size_t>(command_length) + data_length;
    if (total == 0 || total > c.dma_capacity) {
        return SX126X_HAL_STATUS_ERROR;
    }
    if (cownect::radio::sx1262_wait_while_busy(c, c.busy_timeout_ms) != ESP_OK) {
        return SX126X_HAL_STATUS_ERROR;
    }
    std::memcpy(c.dma_tx, command, command_length);
    std::memset(c.dma_tx + command_length, SX126X_NOP, data_length);
    cownect::radio::cs(c, 0);
    esp_err_t err = cownect::radio::transfer(c, total);
    cownect::radio::cs(c, 1);
    if (err != ESP_OK) {
        return SX126X_HAL_STATUS_ERROR;
    }
    std::memcpy(data, c.dma_rx + command_length, data_length);
    return SX126X_HAL_STATUS_OK;
}

extern "C" sx126x_hal_status_t sx126x_hal_reset(const void* context)
{
    auto& c = *static_cast<Sx1262HalContext*>(const_cast<void*>(context));
    return cownect::radio::sx1262_hardware_reset(c) == ESP_OK ? SX126X_HAL_STATUS_OK : SX126X_HAL_STATUS_ERROR;
}

extern "C" sx126x_hal_status_t sx126x_hal_wakeup(const void* context)
{
    auto& c = *static_cast<Sx1262HalContext*>(const_cast<void*>(context));
    // Wake from sleep: NSS falling edge, then a GetStatus (0xC0) and a bounded BUSY wait.
    c.dma_tx[0] = 0xC0;
    c.dma_tx[1] = SX126X_NOP;
    cownect::radio::cs(c, 0);
    esp_err_t err = cownect::radio::transfer(c, 2);
    cownect::radio::cs(c, 1);
    if (err != ESP_OK) {
        return SX126X_HAL_STATUS_ERROR;
    }
    return cownect::radio::sx1262_wait_while_busy(c, c.busy_timeout_ms) == ESP_OK ? SX126X_HAL_STATUS_OK
                                                                                 : SX126X_HAL_STATUS_ERROR;
}
