// accel.c - LIS2DW12 driver: only what the collar needs.
#include "accel.h"
#include "capture.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

static const char *TAG = "accel";

// Registers
#define REG_WHO_AM_I     0x0F
#define REG_CTRL1        0x20
#define REG_CTRL2        0x21
#define REG_CTRL6        0x25
#define REG_OUT_X_L      0x28
#define REG_FIFO_CTRL    0x2E
#define REG_FIFO_SAMPLES 0x2F
#define WHO_AM_I_VALUE   0x44

#define I2C_TIMEOUT_MS   50

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;

// Writes one byte into one sensor register. Returns true on success.
static bool write_reg(uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = {reg, value};
    return i2c_master_transmit(s_dev, buf, 2, I2C_TIMEOUT_MS) == ESP_OK;
}

// Reads `len` bytes starting at register `reg` (the address auto-increments).
static bool read_regs(uint8_t reg, uint8_t *out, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, out, len, I2C_TIMEOUT_MS) == ESP_OK;
}

// Creates the I2C bus and device, checks WHO_AM_I = 0x44, then configures:
// power-down -> BDU + auto-increment -> range -> clear FIFO -> rate (high-performance)
// -> FIFO in continuous mode. Returns false if any step fails.
bool accel_start(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = false,   // the board has 4.7k pull-ups
    };
    if (i2c_new_master_bus(&bus_cfg, &s_bus) != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus init failed");
        return false;
    }
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = ACCEL_I2C_ADDR,
        .scl_speed_hz = ACCEL_I2C_HZ,
    };
    if (i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev) != ESP_OK) {
        accel_stop();
        return false;
    }

    uint8_t who = 0;
    if (!read_regs(REG_WHO_AM_I, &who, 1) || who != WHO_AM_I_VALUE) {
        ESP_LOGE(TAG, "LIS2DW12 not found (WHO_AM_I=0x%02X, expected 0x44)", who);
        accel_stop();
        return false;
    }

    bool ok = write_reg(REG_CTRL1, 0x00)                    // power down while configuring
           && write_reg(REG_CTRL2, 0x0C)                    // BDU + register auto-increment
           && write_reg(REG_CTRL6, ACCEL_FS_BITS)           // range, bandwidth ODR/2
           && write_reg(REG_FIFO_CTRL, 0x00)                // bypass mode = clear FIFO
           && write_reg(REG_CTRL1, ACCEL_ODR_BITS | 0x04)   // rate, high-performance 14-bit
           && write_reg(REG_FIFO_CTRL, 0xC0);               // continuous FIFO mode
    if (!ok) {
        ESP_LOGE(TAG, "configuration failed");
        accel_stop();
        return false;
    }
    ESP_LOGI(TAG, "LIS2DW12 ready: %d Hz, +/-%d g, FIFO polled every %d ms",
             ACCEL_ODR_HZ, ACCEL_RANGE_G, ACCEL_POLL_MS);
    return true;
}

// Reads FIFO_SAMPLES to see how many samples are waiting (and whether the FIFO
// overflowed), then reads each X/Y/Z sample (6 bytes) into c->accel[].
// Samples beyond ACCEL_MAX_SAMPLES are read (to empty the FIFO) but dropped.
void accel_poll(capture_t *c)
{
    uint8_t status;
    if (!read_regs(REG_FIFO_SAMPLES, &status, 1)) return;
    if (status & 0x40) c->accel_overruns++;          // FIFO overflow: we were too slow
    uint8_t waiting = status & 0x3F;                 // number of samples in the FIFO

    uint8_t raw[6];
    for (uint8_t i = 0; i < waiting; i++) {
        if (!read_regs(REG_OUT_X_L, raw, 6)) return;
        if (c->accel_count >= ACCEL_MAX_SAMPLES) continue;   // buffer full: drop
        accel_sample_t *s = &c->accel[c->accel_count++];
        s->x = (int16_t)(raw[0] | (raw[1] << 8));
        s->y = (int16_t)(raw[2] | (raw[3] << 8));
        s->z = (int16_t)(raw[4] | (raw[5] << 8));
    }
}

// Writes CTRL1 = 0 (power-down) and deletes the I2C device and bus.
// Safe to call even if accel_start() failed halfway.
void accel_stop(void)
{
    if (s_dev) {
        write_reg(REG_CTRL1, 0x00);                  // power down
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
    }
    if (s_bus) {
        i2c_del_master_bus(s_bus);
        s_bus = NULL;
    }
}
