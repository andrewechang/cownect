#include "lis2dw12_driver.h"

#include "accelerometer_decode.h"
#include "board_i2c.h"
#include "cownect_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace cownect::sensors {
namespace {
constexpr const char* TAG = "LIS2DW12";
constexpr int kTimeoutMs = static_cast<int>(config::ACCEL_I2C_TIMEOUT_MS);
// Bounded wait for the first FIFO sample after a FIFO mode transition: 5 ODR periods.
constexpr uint32_t kFirstSampleWaitMs = 5 * (1000 / config::ACCEL_ODR_HZ);
}  // namespace

esp_err_t Lis2dw12Driver::ensure_device()
{
    if (dev_ != nullptr) {
        return ESP_OK;
    }
    esp_err_t err = board_i2c_init();
    if (err != ESP_OK) {
        return err;
    }
    i2c_device_config_t cfg = {};
    cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    cfg.device_address = config::LIS2DW12_I2C_ADDR;
    cfg.scl_speed_hz = config::I2C_CLOCK_HZ;
    return i2c_master_bus_add_device(board_i2c_bus(), &cfg, &dev_);
}

esp_err_t Lis2dw12Driver::read_reg(uint8_t reg, uint8_t& value)
{
    esp_err_t err = i2c_master_transmit_receive(dev_, &reg, 1, &value, 1, kTimeoutMs);
    if (err != ESP_OK) {
        stats_.i2c_error_count++;
    }
    return err;
}

esp_err_t Lis2dw12Driver::write_reg(uint8_t reg, uint8_t value)
{
    const uint8_t buf[2] = {reg, value};
    esp_err_t err = i2c_master_transmit(dev_, buf, sizeof(buf), kTimeoutMs);
    if (err != ESP_OK) {
        stats_.i2c_error_count++;
    }
    return err;
}

esp_err_t Lis2dw12Driver::update_reg(uint8_t reg, uint8_t clear_mask, uint8_t set_bits)
{
    uint8_t v = 0;
    esp_err_t err = read_reg(reg, v);
    if (err != ESP_OK) {
        return err;
    }
    v = static_cast<uint8_t>((v & ~clear_mask) | set_bits);
    return write_reg(reg, v);
}

esp_err_t Lis2dw12Driver::read_burst(uint8_t reg, uint8_t* dst, size_t len)
{
    esp_err_t err = i2c_master_transmit_receive(dev_, &reg, 1, dst, len, kTimeoutMs);
    if (err != ESP_OK) {
        stats_.i2c_error_count++;
    }
    return err;
}

esp_err_t Lis2dw12Driver::probe(uint8_t& who_am_i)
{
    esp_err_t err = ensure_device();
    if (err != ESP_OK) {
        return err;
    }
    err = i2c_master_probe(board_i2c_bus(), config::LIS2DW12_I2C_ADDR, kTimeoutMs);
    if (err != ESP_OK) {
        stats_.i2c_error_count++;
        return err;
    }
    return read_reg(lis2dw12::REG_WHO_AM_I, who_am_i);
}

esp_err_t Lis2dw12Driver::set_fifo_mode(uint8_t mode_bits)
{
    const uint8_t v = static_cast<uint8_t>(mode_bits | (config::ACCEL_FIFO_THRESHOLD & lis2dw12::FIFO_FTH_MASK));
    return write_reg(lis2dw12::REG_FIFO_CTRL, v);
}

esp_err_t Lis2dw12Driver::discard_first_fifo_sample()
{
    // Datasheet: the first sample after switching into/out of FIFO mode must be discarded.
    const int64_t deadline = esp_timer_get_time() + static_cast<int64_t>(kFirstSampleWaitMs) * 1000;
    while (esp_timer_get_time() < deadline) {
        uint8_t st = 0;
        esp_err_t err = read_reg(lis2dw12::REG_FIFO_SAMPLES, st);
        if (err != ESP_OK) {
            return err;
        }
        if (decode_fifo_status(st).unread > 0) {
            err = read_burst(lis2dw12::REG_OUT_X_L, burst_, lis2dw12::BYTES_PER_SAMPLE);
            if (err == ESP_OK) {
                stats_.discarded_startup_samples++;
            }
            return err;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    ESP_LOGW(TAG, "no FIFO sample within %u ms after FIFO transition", static_cast<unsigned>(kFirstSampleWaitMs));
    return ESP_ERR_TIMEOUT;
}

esp_err_t Lis2dw12Driver::init()
{
    initialized_ = false;
    capturing_ = false;
    uint8_t who = 0;
    esp_err_t err = probe(who);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "no response at 0x%02X (%s)", config::LIS2DW12_I2C_ADDR, esp_err_to_name(err));
        return err;
    }
    if (who != config::LIS2DW12_WHO_AM_I_VALUE) {
        ESP_LOGE(TAG, "WHO_AM_I=0x%02X expected 0x%02X", who, config::LIS2DW12_WHO_AM_I_VALUE);
        return ESP_ERR_INVALID_RESPONSE;
    }
    using namespace lis2dw12;
    // Power-down before reconfiguration.
    if ((err = update_reg(REG_CTRL1, CTRL1_ODR_MASK, CTRL1_ODR_POWERDOWN)) != ESP_OK) return err;
    if ((err = update_reg(REG_CTRL2, CTRL2_I2C_DISABLE, CTRL2_BDU | CTRL2_IF_ADD_INC)) != ESP_OK) return err;
    if ((err = update_reg(REG_CTRL6, CTRL6_BW_MASK | CTRL6_FS_MASK | CTRL6_FDS, CTRL6_BW_ODR2 | CTRL6_FS_4G)) != ESP_OK) return err;
    if ((err = set_fifo_mode(FIFO_MODE_BYPASS)) != ESP_OK) return err;
    if ((err = update_reg(REG_CTRL1, CTRL1_ODR_MASK | CTRL1_MODE_MASK | CTRL1_LPMODE_MASK,
                          CTRL1_ODR_50HZ | CTRL1_MODE_HP | CTRL1_LPMODE_00)) != ESP_OK) return err;
    if ((err = set_fifo_mode(FIFO_MODE_CONTINUOUS)) != ESP_OK) return err;
    if ((err = discard_first_fifo_sample()) != ESP_OK) return err;
    initialized_ = true;
    ESP_LOGI(TAG, "WHO_AM_I=0x%02X PASS; ODR=50Hz FS=+/-4g mode=HP; FIFO continuous, poll=%ums",
             who, static_cast<unsigned>(config::ACCEL_FIFO_POLL_MS));
    return ESP_OK;
}

esp_err_t Lis2dw12Driver::read_config(Lis2dw12ConfigReadback& out)
{
    using namespace lis2dw12;
    out = {};
    esp_err_t err = ensure_device();
    if (err == ESP_OK) err = read_reg(REG_CTRL1, out.ctrl1);
    if (err == ESP_OK) err = read_reg(REG_CTRL2, out.ctrl2);
    if (err == ESP_OK) err = read_reg(REG_CTRL6, out.ctrl6);
    if (err == ESP_OK) err = read_reg(REG_FIFO_CTRL, out.fifo_ctrl);
    if (err != ESP_OK) {
        return err;
    }
    out.odr_50hz = (out.ctrl1 & CTRL1_ODR_MASK) == CTRL1_ODR_50HZ;
    out.mode_high_performance = (out.ctrl1 & CTRL1_MODE_MASK) == CTRL1_MODE_HP;
    out.fs_4g = (out.ctrl6 & CTRL6_FS_MASK) == CTRL6_FS_4G;
    out.bdu = (out.ctrl2 & CTRL2_BDU) != 0;
    out.if_add_inc = (out.ctrl2 & CTRL2_IF_ADD_INC) != 0;
    out.fifo_continuous = (out.fifo_ctrl & FIFO_MODE_MASK) == FIFO_MODE_CONTINUOUS;
    out.all_ok = out.odr_50hz && out.mode_high_performance && out.fs_4g && out.bdu && out.if_add_inc &&
                 out.fifo_continuous;
    return ESP_OK;
}

esp_err_t Lis2dw12Driver::start_capture()
{
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }
    if (buf_.data == nullptr || buf_.capacity == 0) {
        return ESP_ERR_NO_MEM;
    }
    capturing_ = false;
    buf_.count = 0;
    stats_ = {};
    // Reset FIFO through Bypass, then re-enter Continuous mode.
    esp_err_t err = set_fifo_mode(lis2dw12::FIFO_MODE_BYPASS);
    if (err == ESP_OK) err = set_fifo_mode(lis2dw12::FIFO_MODE_CONTINUOUS);
    stats_.capture_start_us = static_cast<uint64_t>(esp_timer_get_time());
    if (err == ESP_OK) err = discard_first_fifo_sample();
    if (err != ESP_OK) {
        return err;
    }
    capturing_ = true;
    ESP_LOGI(TAG, "capture start");
    return ESP_OK;
}

esp_err_t Lis2dw12Driver::drain(uint8_t unread)
{
    uint8_t remaining = unread > lis2dw12::FIFO_DEPTH ? lis2dw12::FIFO_DEPTH : unread;
    const size_t n_bytes = static_cast<size_t>(remaining) * lis2dw12::BYTES_PER_SAMPLE;
    esp_err_t err = read_burst(lis2dw12::REG_OUT_X_L, burst_, n_bytes);
    if (err != ESP_OK) {
        return err;
    }
    for (uint8_t i = 0; i < remaining; ++i) {
        if (buf_.count < buf_.capacity) {
            buf_.data[buf_.count++] = decode_xyz(&burst_[i * lis2dw12::BYTES_PER_SAMPLE]);
        } else {
            stats_.dropped_samples++;  // never overwrite earlier samples
        }
    }
    stats_.samples_stored = static_cast<uint32_t>(buf_.count);
    return ESP_OK;
}

esp_err_t Lis2dw12Driver::service()
{
    if (!capturing_) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t reg = 0;
    esp_err_t err = read_reg(lis2dw12::REG_FIFO_SAMPLES, reg);
    stats_.fifo_poll_count++;
    if (err != ESP_OK) {
        return err;
    }
    const FifoStatus st = decode_fifo_status(reg);
    if (st.overrun) {
        stats_.fifo_overrun_count++;
    }
    if (st.unread > stats_.max_fifo_level_seen) {
        stats_.max_fifo_level_seen = st.unread;
    }
    if (st.unread == 0) {
        stats_.empty_poll_count++;
        return ESP_OK;
    }
    return drain(st.unread);
}

esp_err_t Lis2dw12Driver::stop_capture()
{
    if (!capturing_) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = service();  // final status read + drain
    stats_.capture_end_us = static_cast<uint64_t>(esp_timer_get_time());
    esp_err_t err2 = set_fifo_mode(lis2dw12::FIFO_MODE_BYPASS);
    capturing_ = false;
    ESP_LOGI(TAG, "capture stop samples=%u fifo_ovr=%u i2c_err=%u dropped=%u",
             static_cast<unsigned>(buf_.count), static_cast<unsigned>(stats_.fifo_overrun_count),
             static_cast<unsigned>(stats_.i2c_error_count), static_cast<unsigned>(stats_.dropped_samples));
    return err != ESP_OK ? err : err2;
}

void Lis2dw12Driver::mark_power_lost()
{
    initialized_ = false;
    capturing_ = false;
}

}  // namespace cownect::sensors
