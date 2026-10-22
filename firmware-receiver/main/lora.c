#include "lora.h"
#include "config.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "lora";

static spi_device_handle_t s_spi = NULL;
static int s_last_packet_rssi = 0;
static float s_last_packet_snr = 0.0f;

#define IRQ_RX_DONE       0x0002
#define IRQ_HEADER_ERR    0x0020
#define IRQ_CRC_ERR       0x0040
#define IRQ_TIMEOUT       0x0200

#define IRQ_ALL           (IRQ_RX_DONE | IRQ_HEADER_ERR | \
                          IRQ_CRC_ERR | IRQ_TIMEOUT)

static bool wait_busy(uint32_t timeout_ms)
{
    int64_t deadline =
        esp_timer_get_time() + ((int64_t)timeout_ms * 1000);

    while (gpio_get_level(PIN_LORA_BUSY) != 0) {
        if (esp_timer_get_time() >= deadline) {
            ESP_LOGE(TAG, "SX1262 BUSY pin timed out");
            return false;
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }

    return true;
}

static bool transfer(const uint8_t *tx,
                     uint8_t *rx,
                     size_t length)
{
    if (!wait_busy(100)) {
        return false;
    }

    spi_transaction_t transaction = {
        .length = length * 8,
        .tx_buffer = tx,
        .rx_buffer = rx
    };

    return spi_device_polling_transmit(s_spi, &transaction) == ESP_OK;
}

static bool command(uint8_t opcode,
                    const uint8_t *parameters,
                    size_t parameter_count)
{
    uint8_t buffer[16] = {0};

    if (parameter_count + 1 > sizeof(buffer)) {
        return false;
    }

    buffer[0] = opcode;

    if (parameter_count > 0) {
        memcpy(&buffer[1], parameters, parameter_count);
    }

    return transfer(buffer, NULL, parameter_count + 1);
}

static bool write_register(uint16_t address,
                           const uint8_t *data,
                           size_t length)
{
    uint8_t buffer[8] = {0};

    if (length + 3 > sizeof(buffer)) {
        return false;
    }

    buffer[0] = 0x0D;
    buffer[1] = (uint8_t)(address >> 8);
    buffer[2] = (uint8_t)(address & 0xFF);

    memcpy(&buffer[3], data, length);

    return transfer(buffer, NULL, length + 3);
}

static uint16_t get_irq_status(void)
{
    uint8_t tx[4] = {0x12, 0x00, 0x00, 0x00};
    uint8_t rx[4] = {0};

    if (!transfer(tx, rx, sizeof(tx))) {
        return 0;
    }

    return ((uint16_t)rx[2] << 8) | rx[3];
}

static bool get_packet_status(int *rssi_dbm,
                             float *snr_db)
{
    uint8_t tx[4] = {0x14, 0x00, 0x00, 0x00};
    uint8_t rx[4] = {0};

    if (!transfer(tx, rx, sizeof(tx))) {
        return false;
    }

    /*
     * SX1262 response:
     * rx[2] = Packet RSSI
     * rx[3] = Packet SNR
     */
    *rssi_dbm = -((int)rx[2] / 2);
    *snr_db = ((int8_t)rx[3]) / 4.0f;

    return true;
}

static bool read_buffer_status(uint8_t *payload_length,
                               uint8_t *buffer_start)
{
    uint8_t tx[4] = {0x13, 0x00, 0x00, 0x00};
    uint8_t rx[4] = {0};

    if (!transfer(tx, rx, sizeof(tx))) {
        return false;
    }

    *payload_length = rx[2];
    *buffer_start = rx[3];

    return true;
}

static bool read_buffer(uint8_t start,
                        uint8_t *data,
                        size_t length)
{
    uint8_t tx[3 + LORA_MAX_PACKET] = {0};
    uint8_t rx[3 + LORA_MAX_PACKET] = {0};

    if (length > LORA_MAX_PACKET) {
        return false;
    }

    tx[0] = 0x1E;
    tx[1] = start;
    tx[2] = 0x00;

    if (!transfer(tx, rx, length + 3)) {
        return false;
    }

    memcpy(data, &rx[3], length);

    return true;
}

static void clear_irq_status(void)
{
    uint8_t parameters[2] = {0xFF, 0xFF};
    command(0x02, parameters, sizeof(parameters));
}

static void set_standby(void)
{
    uint8_t parameter = 0x00;
    command(0x80, &parameter, 1);
}

static uint16_t wait_for_irq(uint32_t timeout_ms)
{
    int64_t deadline =
        esp_timer_get_time() + ((int64_t)timeout_ms * 1000);

    while (esp_timer_get_time() < deadline) {
        if (gpio_get_level(PIN_LORA_DIO1) != 0) {
            return get_irq_status();
        }

        /*
         * This delay is important. Without it, the main task can
         * starve IDLE0 and trigger the ESP task watchdog.
         */
        vTaskDelay(pdMS_TO_TICKS(10));
        taskYIELD();
    }

    return 0;
}

static bool start_bus(void)
{
    if (s_spi != NULL) {
        return true;
    }

    gpio_config_t input_config = {
        .pin_bit_mask =
            (1ULL << PIN_LORA_BUSY) |
            (1ULL << PIN_LORA_DIO1),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    if (gpio_config(&input_config) != ESP_OK) {
        return false;
    }

    gpio_set_pull_mode(PIN_LORA_BUSY, GPIO_PULLDOWN_ONLY);
    gpio_set_pull_mode(PIN_LORA_DIO1, GPIO_PULLDOWN_ONLY);

    gpio_config_t reset_config = {
        .pin_bit_mask = 1ULL << PIN_LORA_RST,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    if (gpio_config(&reset_config) != ESP_OK) {
        return false;
    }

    spi_bus_config_t bus_config = {
        .mosi_io_num = PIN_LORA_MOSI,
        .miso_io_num = PIN_LORA_MISO,
        .sclk_io_num = PIN_LORA_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 300
    };

    esp_err_t error =
        spi_bus_initialize(SPI2_HOST,
                           &bus_config,
                           SPI_DMA_CH_AUTO);

    if (error != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus initialization failed: %s",
                 esp_err_to_name(error));
        return false;
    }

    spi_device_interface_config_t device_config = {
        .clock_speed_hz = LORA_SPI_HZ,
        .mode = 0,
        .spics_io_num = PIN_LORA_CS,
        .queue_size = 1
    };

    error = spi_bus_add_device(SPI2_HOST,
                               &device_config,
                               &s_spi);

    if (error != ESP_OK) {
        ESP_LOGE(TAG, "SPI device initialization failed: %s",
                 esp_err_to_name(error));
        spi_bus_free(SPI2_HOST);
        return false;
    }

    return true;
}

static bool reset_radio(void)
{
    gpio_set_level(PIN_LORA_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(5));

    gpio_set_level(PIN_LORA_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(20));

    return wait_busy(100);
}

static uint8_t get_bandwidth_code(void)
{
    switch (LORA_BANDWIDTH_KHZ) {
        case 125:
            return 0x04;

        case 250:
            return 0x05;

        case 500:
            return 0x06;

        default:
            return 0xFF;
    }
}

static void calibrate_image(void)
{
    uint32_t frequency_mhz =
        LORA_FREQUENCY_HZ / 1000000;

    uint8_t values[2];

    if (frequency_mhz > 900) {
        values[0] = 0xE1;
        values[1] = 0xE9;
    } else if (frequency_mhz > 850) {
        values[0] = 0xD7;
        values[1] = 0xDB;
    } else if (frequency_mhz > 770) {
        values[0] = 0xC1;
        values[1] = 0xC5;
    } else if (frequency_mhz > 460) {
        values[0] = 0x75;
        values[1] = 0x81;
    } else {
        values[0] = 0x6B;
        values[1] = 0x6F;
    }

    command(0x98, values, sizeof(values));
}

static bool set_packet_parameters(uint8_t maximum_payload_length)
{
    uint8_t parameters[6] = {
        (uint8_t)(LORA_PREAMBLE_LEN >> 8),
        (uint8_t)(LORA_PREAMBLE_LEN & 0xFF),
        0x00,                       // Explicit header
        maximum_payload_length,
        0x01,                       // CRC enabled
        0x00                        // Standard IQ
    };

    return command(0x8C, parameters, sizeof(parameters));
}

static bool configure_radio(void)
{
    bool success = true;
    uint8_t parameters[8] = {0};

    // SetStandby(RC)
    parameters[0] = 0x00;
    success &= command(0x80, parameters, 1);

#if LORA_USE_TCXO
    // SetDIO3AsTCXOCtrl: 1.8 V, 5 ms startup
    parameters[0] = LORA_TCXO_VOLTAGE;
    parameters[1] = 0x00;
    parameters[2] = 0x01;
    parameters[3] = 0x40;
    success &= command(0x97, parameters, 4);
#endif

    // SetRegulatorMode
    parameters[0] = LORA_USE_DCDC ? 0x01 : 0x00;
    success &= command(0x96, parameters, 1);

    // Calibrate all blocks
    parameters[0] = 0x7F;
    success &= command(0x89, parameters, 1);

    vTaskDelay(pdMS_TO_TICKS(10));

#if LORA_USE_DIO2_RF_SWITCH
    // Enable DIO2 RF switch control
    parameters[0] = 0x01;
    success &= command(0x9D, parameters, 1);
#endif

    // SetPacketType(LoRa)
    parameters[0] = 0x01;
    success &= command(0x8A, parameters, 1);

    calibrate_image();

    // SetRfFrequency
    uint32_t frequency_register =
        (uint32_t)(((uint64_t)LORA_FREQUENCY_HZ << 25) /
                   32000000ULL);

    parameters[0] = frequency_register >> 24;
    parameters[1] = frequency_register >> 16;
    parameters[2] = frequency_register >> 8;
    parameters[3] = frequency_register;

    success &= command(0x86, parameters, 4);

    // SetBufferBaseAddress
    parameters[0] = 0x00;
    parameters[1] = 0x00;
    success &= command(0x8F, parameters, 2);

    // SetModulationParams
    uint32_t symbol_time_us =
        (1000U << LORA_SPREADING_FACTOR) /
        LORA_BANDWIDTH_KHZ;

    parameters[0] = LORA_SPREADING_FACTOR;
    parameters[1] = get_bandwidth_code();
    parameters[2] = LORA_CODING_RATE - 4;
    parameters[3] = symbol_time_us > 16000 ? 0x01 : 0x00;

    success &= command(0x8B, parameters, 4);

    // Set the private LoRa sync word.
    uint8_t sync_word[2] = {
        (uint8_t)(LORA_SYNC_WORD >> 8),
        (uint8_t)(LORA_SYNC_WORD & 0xFF)
    };

    success &= write_register(0x0740,
                              sync_word,
                              sizeof(sync_word));

    // Map RX_DONE, CRC_ERR, HEADER_ERR, and TIMEOUT to DIO1.
    parameters[0] = IRQ_ALL >> 8;
    parameters[1] = IRQ_ALL & 0xFF;

    parameters[2] = IRQ_ALL >> 8;
    parameters[3] = IRQ_ALL & 0xFF;

    parameters[4] = 0x00;
    parameters[5] = 0x00;
    parameters[6] = 0x00;
    parameters[7] = 0x00;

    success &= command(0x08, parameters, 8);

    return success;
}

bool lora_config_ready(void)
{
    if (LORA_FREQUENCY_HZ == 0) {
        return false;
    }

    if (LORA_BANDWIDTH_KHZ == 0) {
        return false;
    }

    if (get_bandwidth_code() == 0xFF) {
        ESP_LOGE(TAG, "Unsupported LoRa bandwidth");
        return false;
    }

    if (LORA_SPREADING_FACTOR < 5 ||
        LORA_SPREADING_FACTOR > 12) {
        return false;
    }

    if (LORA_CODING_RATE < 5 ||
        LORA_CODING_RATE > 8) {
        return false;
    }

    if (!LORA_ANTENNA_CONNECTED) {
        ESP_LOGE(TAG, "Antenna is not configured as connected");
        return false;
    }

    return true;
}

bool lora_begin(void)
{
    if (!lora_config_ready()) {
        ESP_LOGE(TAG, "Invalid LoRa configuration");
        return false;
    }

    if (!start_bus()) {
        return false;
    }

    if (!reset_radio()) {
        ESP_LOGE(TAG, "SX1262 reset failed");
        return false;
    }

    if (!configure_radio()) {
        ESP_LOGE(TAG, "SX1262 configuration failed");
        return false;
    }

    ESP_LOGI(TAG, "SX1262 configured successfully");

    return true;
}

int lora_receive(uint8_t *buffer,
                 size_t buffer_size,
                 uint32_t timeout_ms)
{
    if (buffer == NULL ||
        buffer_size == 0 ||
        buffer_size > LORA_MAX_PACKET) {
        return -1;
    }

    /*
     * SX1262 timeout units are 15.625 microseconds.
     * timeout_ms * 64 converts milliseconds to radio units.
     */
    uint32_t radio_timeout = timeout_ms * 64;

    uint8_t timeout_parameters[3] = {
        (uint8_t)(radio_timeout >> 16),
        (uint8_t)(radio_timeout >> 8),
        (uint8_t)radio_timeout
    };

    clear_irq_status();

    if (!set_packet_parameters(LORA_MAX_PACKET)) {
        return -1;
    }

    // SetRx
    if (!command(0x82, timeout_parameters, 3)) {
        return -1;
    }

    uint16_t irq_status =
        wait_for_irq(timeout_ms + 100);

    clear_irq_status();

    if ((irq_status & IRQ_RX_DONE) == 0) {
        set_standby();
        return -1;
    }

    if ((irq_status & IRQ_CRC_ERR) != 0 ||
        (irq_status & IRQ_HEADER_ERR) != 0) {
        set_standby();
        return -1;
    }

    uint8_t packet_length = 0;
    uint8_t buffer_start = 0;

    if (!read_buffer_status(&packet_length,
                            &buffer_start)) {
        set_standby();
        return -1;
    }

    if (packet_length > buffer_size) {
        set_standby();
        ESP_LOGW(TAG, "Received packet is too large: %u",
                 packet_length);
        return -1;
    }

    if (!read_buffer(buffer_start,
                     buffer,
                     packet_length)) {
        set_standby();
        return -1;
    }

    get_packet_status(&s_last_packet_rssi,
                      &s_last_packet_snr);

    return packet_length;
}

int lora_last_packet_rssi(void)
{
    return s_last_packet_rssi;
}

float lora_last_packet_snr(void)
{
    return s_last_packet_snr;
}

void lora_end(void)
{
    if (s_spi == NULL) {
        return;
    }

    set_standby();

    spi_bus_remove_device(s_spi);
    spi_bus_free(SPI2_HOST);

    s_spi = NULL;
}