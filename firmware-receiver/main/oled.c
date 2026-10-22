#include "oled.h"

#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define OLED_SDA_GPIO       17
#define OLED_SCL_GPIO       18
#define OLED_RESET_GPIO     21
#define OLED_VEXT_GPIO      36

#define OLED_I2C_PORT       I2C_NUM_0
#define OLED_I2C_ADDRESS    0x3C
#define OLED_WIDTH          128

static i2c_master_bus_handle_t s_i2c_bus = NULL;
static i2c_master_dev_handle_t s_oled = NULL;
static bool s_oled_ready = false;

static esp_err_t oled_write_command(const uint8_t *commands,
                                    size_t count)
{
    uint8_t data[32];

    if (count + 1 > sizeof(data)) {
        return ESP_ERR_INVALID_SIZE;
    }

    data[0] = 0x00;
    memcpy(&data[1], commands, count);

    return i2c_master_transmit(
        s_oled,
        data,
        count + 1,
        1000
    );
}

static void glyph(char character, uint8_t output[5])
{
    memset(output, 0, 5);

    switch (character) {
        case 'A': memcpy(output, (uint8_t[]){0x7E,0x11,0x11,0x11,0x7E}, 5); break;
        case 'C': memcpy(output, (uint8_t[]){0x3E,0x41,0x41,0x41,0x22}, 5); break;
        case 'D': memcpy(output, (uint8_t[]){0x7F,0x41,0x41,0x22,0x1C}, 5); break;
        case 'E': memcpy(output, (uint8_t[]){0x7F,0x49,0x49,0x49,0x41}, 5); break;
        case 'G': memcpy(output, (uint8_t[]){0x3E,0x41,0x49,0x49,0x7A}, 5); break;
        case 'I': memcpy(output, (uint8_t[]){0x00,0x41,0x7F,0x41,0x00}, 5); break;
        case 'K': memcpy(output, (uint8_t[]){0x7F,0x08,0x14,0x22,0x41}, 5); break;
        case 'L': memcpy(output, (uint8_t[]){0x7F,0x40,0x40,0x40,0x40}, 5); break;
        case 'N': memcpy(output, (uint8_t[]){0x7F,0x04,0x08,0x10,0x7F}, 5); break;
        case 'O': memcpy(output, (uint8_t[]){0x3E,0x41,0x41,0x41,0x3E}, 5); break;
        case 'P': memcpy(output, (uint8_t[]){0x7F,0x09,0x09,0x09,0x06}, 5); break;
        case 'R': memcpy(output, (uint8_t[]){0x7F,0x09,0x19,0x29,0x46}, 5); break;
        case 'S': memcpy(output, (uint8_t[]){0x46,0x49,0x49,0x49,0x31}, 5); break;
        case 'T': memcpy(output, (uint8_t[]){0x01,0x01,0x7F,0x01,0x01}, 5); break;
        case 'W': memcpy(output, (uint8_t[]){0x7F,0x20,0x18,0x20,0x7F}, 5); break;

        case '0': memcpy(output, (uint8_t[]){0x3E,0x45,0x49,0x51,0x3E}, 5); break;
        case '1': memcpy(output, (uint8_t[]){0x00,0x21,0x7F,0x01,0x00}, 5); break;
        case '2': memcpy(output, (uint8_t[]){0x23,0x45,0x49,0x51,0x21}, 5); break;
        case '3': memcpy(output, (uint8_t[]){0x22,0x41,0x49,0x49,0x36}, 5); break;
        case '4': memcpy(output, (uint8_t[]){0x0C,0x14,0x24,0x7F,0x04}, 5); break;
        case '5': memcpy(output, (uint8_t[]){0x72,0x51,0x51,0x51,0x4E}, 5); break;
        case '6': memcpy(output, (uint8_t[]){0x1E,0x29,0x49,0x49,0x06}, 5); break;
        case '7': memcpy(output, (uint8_t[]){0x40,0x47,0x48,0x50,0x60}, 5); break;
        case '8': memcpy(output, (uint8_t[]){0x36,0x49,0x49,0x49,0x36}, 5); break;
        case '9': memcpy(output, (uint8_t[]){0x30,0x49,0x49,0x4A,0x3C}, 5); break;

        case ':': memcpy(output, (uint8_t[]){0x00,0x36,0x36,0x00,0x00}, 5); break;
        case '-': memcpy(output, (uint8_t[]){0x08,0x08,0x08,0x08,0x08}, 5); break;
        case '.': memcpy(output, (uint8_t[]){0x00,0x60,0x60,0x00,0x00}, 5); break;

        default:
            break;
    }
}

static esp_err_t oled_write_data(const uint8_t *data,
                                 size_t length)
{
    uint8_t buffer[130];

    if (length + 1 > sizeof(buffer)) {
        return ESP_ERR_INVALID_SIZE;
    }

    buffer[0] = 0x40;
    memcpy(&buffer[1], data, length);

    return i2c_master_transmit(
        s_oled,
        buffer,
        length + 1,
        1000
    );
}

static void oled_write_line(uint8_t page,
                            const char *text)
{
    uint8_t pixels[OLED_WIDTH] = {0};
    size_t column = 0;

    for (size_t index = 0;
         text[index] != '\0' &&
         column + 6 <= OLED_WIDTH;
         index++) {

        uint8_t character[5];

        glyph(text[index], character);

        for (int i = 0; i < 5; i++) {
            pixels[column++] = character[i];
        }

        pixels[column++] = 0x00;
    }

    uint8_t position[] = {
        (uint8_t)(0xB0 | page),
        0x00,
        0x10
    };

    if (oled_write_command(position,
                           sizeof(position)) != ESP_OK) {
        return;
    }

    oled_write_data(pixels, sizeof(pixels));
}

bool oled_init(void)
{
    gpio_config_t output_config = {
        .pin_bit_mask =
            (1ULL << OLED_VEXT_GPIO) |
            (1ULL << OLED_RESET_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    if (gpio_config(&output_config) != ESP_OK) {
        return false;
    }

    // Heltec V3 VEXT is active-low.
    gpio_set_level(OLED_VEXT_GPIO, 0);

    gpio_set_level(OLED_RESET_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(20));

    gpio_set_level(OLED_RESET_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(20));

    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = OLED_SDA_GPIO,
        .scl_io_num = OLED_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true
    };

    if (i2c_new_master_bus(&bus_config, &s_i2c_bus) != ESP_OK) {
        return false;
    }

    i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = OLED_I2C_ADDRESS,
        .scl_speed_hz = 400000
    };

    if (i2c_master_bus_add_device(
            s_i2c_bus,
            &device_config,
            &s_oled) != ESP_OK) {
        return false;
    }

    const uint8_t initialization[] = {
        0xAE,
        0xD5, 0x80,
        0xA8, 0x3F,
        0xD3, 0x00,
        0x40,
        0x8D, 0x14,
        0x20, 0x00,
        0xA1,
        0xC8,
        0xDA, 0x12,
        0x81, 0x8F,
        0xD9, 0xF1,
        0xDB, 0x40,
        0xA4,
        0xA6,
        0xAF
    };

    if (oled_write_command(
            initialization,
            sizeof(initialization)) != ESP_OK) {
        return false;
    }

    s_oled_ready = true;

    oled_write_line(0, "CowNect");
    oled_write_line(1, "WAITING");
    oled_write_line(2, "");
    oled_write_line(3, "");
    oled_write_line(4, "");
    oled_write_line(5, "");
    oled_write_line(6, "");
    oled_write_line(7, "");

    return true;
}

void oled_show_status(uint32_t packet_count,
                      uint64_t collar_id,
                      uint32_t capture_id,
                      int rssi_dbm,
                      float snr_db,
                      bool has_packet)
{
    if (!s_oled_ready) {
        return;
    }

    char line[24];

    oled_write_line(0,
                    has_packet ? "PACKET OK" : "WAITING");

    snprintf(line,
             sizeof(line),
             "PKT:%lu",
             (unsigned long)packet_count);
    oled_write_line(1, line);

    snprintf(line,
             sizeof(line),
             "ID:%llu",
             (unsigned long long)collar_id);
    oled_write_line(2, line);

    snprintf(line,
             sizeof(line),
             "RSSI:%d",
             rssi_dbm);
    oled_write_line(3, line);

    snprintf(line,
             sizeof(line),
             "SNR:%.1f",
             snr_db);
    oled_write_line(4, line);

    snprintf(line,
             sizeof(line),
             "CAP:%lu",
             (unsigned long)capture_id);
    oled_write_line(5, line);
    oled_write_line(6, "");
    oled_write_line(7, "");
}