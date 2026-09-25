// accel.c - LIS2DW12 accelerometer: only the few registers the collar needs.
#include "accel.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

static const char *TAG = "accel";

#define REG_WHO_AM_I      0x0F    // chip ID, must read 0x44
#define REG_CTRL1         0x20    // rate + mode
#define REG_CTRL2         0x21    // BDU + address auto-increment
#define REG_CTRL6         0x25    // range
#define REG_OUT_X_L       0x28    // first data byte (X low)
#define REG_FIFO_CTRL     0x2E    // FIFO mode
#define REG_FIFO_SAMPLES  0x2F    // bit 6 = overflow, bits 0-5 = samples waiting

static i2c_master_bus_handle_t bus;
static i2c_master_dev_handle_t sensor;

// Writes one sensor register.
static bool write_reg(uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = {reg, value};
    return i2c_master_transmit(sensor, buf, 2, 50) == ESP_OK;
}

// Reads len bytes starting at register reg.
static bool read_regs(uint8_t reg, uint8_t *out, size_t len)
{
    return i2c_master_transmit_receive(sensor, &reg, 1, out, len, 50) == ESP_OK;
}

// Opens the I2C bus, checks the chip ID, then sets range, rate and FIFO mode.
bool accel_start(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0, .sda_io_num = PIN_I2C_SDA, .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = false,          // the board has 4.7k pull-ups
    };
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = ACCEL_I2C_ADDR, .scl_speed_hz = ACCEL_I2C_HZ,
    };
    if (i2c_new_master_bus(&bus_cfg, &bus) != ESP_OK ||
        i2c_master_bus_add_device(bus, &dev_cfg, &sensor) != ESP_OK) {
        ESP_LOGE(TAG, "I2C start failed");
        accel_stop();
        return false;
    }

    uint8_t id = 0;
    if (!read_regs(REG_WHO_AM_I, &id, 1) || id != 0x44) {
        ESP_LOGE(TAG, "LIS2DW12 not found (ID 0x%02X, expected 0x44)", id);
        accel_stop();
        return false;
    }

    bool ok = write_reg(REG_CTRL1, 0x00)                       // power down while changing settings
           && write_reg(REG_CTRL2, 0x0C)                       // BDU + auto-increment
           && write_reg(REG_CTRL6, ACCEL_RANGE_BITS)           // range
           && write_reg(REG_FIFO_CTRL, 0x00)                   // clear the FIFO
           && write_reg(REG_CTRL1, ACCEL_RATE_BITS | 0x04)     // rate, high-performance 14-bit
           && write_reg(REG_FIFO_CTRL, 0xC0);                  // FIFO continuous mode
    if (!ok) {
        ESP_LOGE(TAG, "configuration failed");
        accel_stop();
        return false;
    }
    ESP_LOGI(TAG, "ready: %d Hz, +/-%d g", ACCEL_RATE_HZ, ACCEL_RANGE_G);
    return true;
}

// Asks how many samples are waiting (and whether the FIFO overflowed), then reads them.
void accel_read_fifo(accel_data_t *a)
{
    uint8_t status;
    if (!read_regs(REG_FIFO_SAMPLES, &status, 1)) return;
    if (status & 0x40) a->overruns++;                   // too slow: samples were lost
    int waiting = status & 0x3F;

    for (int i = 0; i < waiting; i++) {
        uint8_t raw[6];
        if (!read_regs(REG_OUT_X_L, raw, 6)) return;
        if (a->count >= ACCEL_MAX_SAMPLES) continue;    // buffer full: read but drop
        a->samples[a->count].x = (int16_t)(raw[0] | (raw[1] << 8));
        a->samples[a->count].y = (int16_t)(raw[2] | (raw[3] << 8));
        a->samples[a->count].z = (int16_t)(raw[4] | (raw[5] << 8));
        a->count++;
    }
}

// Powers the sensor down and frees the I2C bus (safe to call after a failed start).
void accel_stop(void)
{
    if (sensor) {
        write_reg(REG_CTRL1, 0x00);
        i2c_master_bus_rm_device(sensor);
        sensor = NULL;
    }
    if (bus) {
        i2c_del_master_bus(bus);
        bus = NULL;
    }
}
