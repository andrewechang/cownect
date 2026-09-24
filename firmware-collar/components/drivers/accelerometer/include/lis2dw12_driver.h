#pragma once

#include "accelerometer.h"
#include "driver/i2c_master.h"
#include "lis2dw12_regs.h"

namespace cownect::sensors {

struct Lis2dw12ConfigReadback {
    uint8_t ctrl1;
    uint8_t ctrl2;
    uint8_t ctrl6;
    uint8_t fifo_ctrl;
    bool odr_50hz;
    bool mode_high_performance;
    bool fs_4g;
    bool bdu;
    bool if_add_inc;
    bool fifo_continuous;
    bool all_ok;
};

// LIS2DW12 on I2C 0x18, 50 Hz / +/-4 g / High-Performance, FIFO continuous, bounded polling
// (no INT1/INT2 on this PCB). Writes into caller-owned storage; never toggles PERIPH_EN.
class Lis2dw12Driver final : public IAccelerometer {
public:
    explicit Lis2dw12Driver(AccelCaptureBuffer& buffer) : buf_(buffer) {}

    // Requires SWITCHED_3V3 commanded on and stabilized by the caller.
    esp_err_t init() override;
    esp_err_t start_capture() override;
    esp_err_t service() override;
    esp_err_t stop_capture() override;

    bool is_capturing() const override { return capturing_; }
    size_t sample_count() const override { return buf_.count; }
    const AccelSample* samples() const override { return buf_.data; }
    const AccelerometerCaptureStats& stats() const override { return stats_; }

    // Identification only (Material 4 stage 4.6 / test 5.1).
    esp_err_t probe(uint8_t& who_am_i);
    // Register readback (test 5.2). Reserved bits are not compared.
    esp_err_t read_config(Lis2dw12ConfigReadback& out);
    // Marks the driver uninitialized, e.g. after SWITCHED_3V3 was commanded off.
    void mark_power_lost();

private:
    esp_err_t ensure_device();
    esp_err_t read_reg(uint8_t reg, uint8_t& value);
    esp_err_t write_reg(uint8_t reg, uint8_t value);
    esp_err_t update_reg(uint8_t reg, uint8_t clear_mask, uint8_t set_bits);
    esp_err_t read_burst(uint8_t reg, uint8_t* dst, size_t len);
    esp_err_t set_fifo_mode(uint8_t mode_bits);
    esp_err_t discard_first_fifo_sample();
    esp_err_t drain(uint8_t unread);

    AccelCaptureBuffer& buf_;
    AccelerometerCaptureStats stats_ = {};
    i2c_master_dev_handle_t dev_ = nullptr;
    bool initialized_ = false;
    bool capturing_ = false;
    uint8_t burst_[lis2dw12::FIFO_DEPTH * lis2dw12::BYTES_PER_SAMPLE] = {};
};

}  // namespace cownect::sensors
