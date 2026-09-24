#pragma once

// Private ESP-IDF HAL for the vendored Semtech sx126x_driver (Material 12 sections 8-11).
// The `context` pointer passed to every sx126x_* call is a Sx1262HalContext*.

#include <cstdint>
#include "driver/spi_master.h"
#include "esp_err.h"

namespace cownect::radio {

struct Sx1262HalContext {
    spi_device_handle_t spi;
    int pin_cs;
    int pin_reset;
    int pin_busy;
    uint32_t busy_timeout_ms;

    uint8_t* dma_tx;  // internal DMA-capable scratch buffers
    uint8_t* dma_rx;
    size_t dma_capacity;

    // Diagnostics (task context only).
    uint32_t busy_timeout_count;
    uint32_t spi_error_count;
    uint32_t reset_count;
    uint32_t last_busy_wait_us;
    esp_err_t last_error;
};

// Bounded BUSY wait: returns COWNECT_ERR_BUSY_TIMEOUT instead of hanging.
esp_err_t sx1262_wait_while_busy(Sx1262HalContext& ctx, uint32_t timeout_ms);
// Hardware reset through GPIO13 followed by a bounded BUSY wait.
esp_err_t sx1262_hardware_reset(Sx1262HalContext& ctx);

}  // namespace cownect::radio
