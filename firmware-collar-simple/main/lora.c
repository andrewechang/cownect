// lora.c - talks to the SX1262 with its SPI command set (SX1261/2 datasheet, chapter 13).
//
// Every command is: wait until BUSY is low -> one SPI transfer [opcode, parameters...].
#include "lora.h"
#include <string.h>
#include "config.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "lora";

static spi_device_handle_t s_spi;

// IRQ flags (datasheet table 13-29)
#define IRQ_TX_DONE     0x0001
#define IRQ_RX_DONE     0x0002
#define IRQ_HEADER_ERR  0x0020
#define IRQ_CRC_ERR     0x0040
#define IRQ_TIMEOUT     0x0200
#define IRQ_ALL         (IRQ_TX_DONE | IRQ_RX_DONE | IRQ_HEADER_ERR | IRQ_CRC_ERR | IRQ_TIMEOUT)

// ---------------------------------------------------------------- low level
// Waits until the radio's BUSY pin goes low (ready for a command).
// Returns false if it stays high longer than timeout_ms.
static bool wait_busy(uint32_t timeout_ms)
{
    int64_t end = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    while (gpio_get_level(PIN_LORA_BUSY)) {
        if (esp_timer_get_time() > end) {
            ESP_LOGE(TAG, "BUSY stuck high");
            return false;
        }
    }
    return true;
}

// Sends tx (len bytes); the bytes clocked back are written to rx (may be NULL).
static bool transfer(const uint8_t *tx, uint8_t *rx, size_t len)
{
    if (!wait_busy(100)) return false;
    spi_transaction_t t = { .length = len * 8, .tx_buffer = tx, .rx_buffer = rx };
    return spi_device_polling_transmit(s_spi, &t) == ESP_OK;
}

// Sends one command: opcode followed by n parameter bytes.
static bool cmd(uint8_t opcode, const uint8_t *params, size_t n)
{
    uint8_t buf[16];
    buf[0] = opcode;
    if (n) memcpy(&buf[1], params, n);
    return transfer(buf, NULL, n + 1);
}

// Writes n bytes into radio registers starting at addr (WriteRegister 0x0D).
static bool write_register(uint16_t addr, const uint8_t *data, size_t n)
{
    uint8_t buf[8] = {0x0D, addr >> 8, addr & 0xFF};
    memcpy(&buf[3], data, n);
    return transfer(buf, NULL, n + 3);
}

// Reads n bytes from radio registers starting at addr (ReadRegister 0x1D).
static bool read_register(uint16_t addr, uint8_t *data, size_t n)
{
    uint8_t tx[8] = {0x1D, addr >> 8, addr & 0xFF, 0};   // opcode, address, one status byte
    uint8_t rx[8] = {0};
    if (!transfer(tx, rx, n + 4)) return false;
    memcpy(data, &rx[4], n);
    return true;
}

// Returns the radio's interrupt flags (GetIrqStatus 0x12), e.g. IRQ_TX_DONE.
static uint16_t get_irq_status(void)
{
    uint8_t tx[4] = {0x12, 0, 0, 0}, rx[4] = {0};
    if (!transfer(tx, rx, 4)) return 0;
    return (rx[2] << 8) | rx[3];
}

// ---------------------------------------------------------------- setup
// Sets up the BUSY/DIO1 inputs, the RESET output and the SPI bus + device.
// Does nothing if the bus is already running.
static bool bus_start(void)
{
    if (s_spi) return true;
    gpio_config_t in = { .pin_bit_mask = (1ULL << PIN_LORA_BUSY) | (1ULL << PIN_LORA_DIO1), .mode = GPIO_MODE_INPUT };
    gpio_config(&in);
    gpio_config_t out = { .pin_bit_mask = 1ULL << PIN_LORA_RST, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&out);

    spi_bus_config_t bus = {
        .mosi_io_num = PIN_LORA_MOSI, .miso_io_num = PIN_LORA_MISO, .sclk_io_num = PIN_LORA_SCK,
        .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = 300,
    };
    if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return false;
    spi_device_interface_config_t dev = {
        .clock_speed_hz = LORA_SPI_HZ, .mode = 0, .spics_io_num = PIN_LORA_CS, .queue_size = 1,
    };
    if (spi_bus_add_device(SPI2_HOST, &dev, &s_spi) != ESP_OK) {
        spi_bus_free(SPI2_HOST);
        return false;
    }
    return true;
}

// Removes the SPI device and frees the bus.
static void bus_stop(void)
{
    if (!s_spi) return;
    spi_bus_remove_device(s_spi);
    spi_bus_free(SPI2_HOST);
    s_spi = NULL;
}

// Pulses RESET low for 2 ms, waits 10 ms and then until BUSY is low.
static bool reset_radio(void)
{
    gpio_set_level(PIN_LORA_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(2));
    gpio_set_level(PIN_LORA_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    return wait_busy(100);
}

// Checks every TODO setting in lora.h and the antenna flag (see lora.h).
bool lora_config_ready(void)
{
    const char *missing = NULL;
    if (LORA_FREQUENCY_HZ == 0) missing = "LORA_FREQUENCY_HZ";
    else if (LORA_BANDWIDTH_KHZ == 0) missing = "LORA_BANDWIDTH_KHZ";
    else if (LORA_SPREADING_FACTOR == 0) missing = "LORA_SPREADING_FACTOR";
    else if (LORA_CODING_RATE == 0) missing = "LORA_CODING_RATE";
    else if (LORA_TX_POWER_DBM == 0) missing = "LORA_TX_POWER_DBM";
    else if (LORA_SYNC_WORD == 0) missing = "LORA_SYNC_WORD";
    else if (LORA_USE_TCXO < 0) missing = "LORA_USE_TCXO";
    else if (LORA_USE_DIO2_RF_SWITCH < 0) missing = "LORA_USE_DIO2_RF_SWITCH";
    else if (LORA_USE_DCDC < 0) missing = "LORA_USE_DCDC";
    else if (LORA_TX_TIMEOUT_MS == 0) missing = "LORA_TX_TIMEOUT_MS";
    else if (!LORA_ANTENNA_CONNECTED) missing = "LORA_ANTENNA_CONNECTED";
    if (missing) {
        ESP_LOGW(TAG, "LoRa skipped: %s is not set in lora.h", missing);
        return false;
    }
    return true;
}

// Converts LORA_BANDWIDTH_KHZ to the SX1262 code (0xFF = unsupported value).
static uint8_t bandwidth_code(void)
{
    switch (LORA_BANDWIDTH_KHZ) {
    case 125: return 0x04;
    case 250: return 0x05;
    case 500: return 0x06;
    default:  return 0xFF;
    }
}

// Calibrates the receiver image rejection for the band containing
// LORA_FREQUENCY_HZ (datasheet table 9-2).
static void calibrate_image(void)
{
    uint8_t f[2];
    uint32_t mhz = LORA_FREQUENCY_HZ / 1000000;
    if (mhz > 900)      { f[0] = 0xE1; f[1] = 0xE9; }
    else if (mhz > 850) { f[0] = 0xD7; f[1] = 0xDB; }
    else if (mhz > 770) { f[0] = 0xC1; f[1] = 0xC5; }
    else if (mhz > 460) { f[0] = 0x75; f[1] = 0x81; }
    else                { f[0] = 0x6B; f[1] = 0x6F; }
    cmd(0x98, f, 2);
}

// Sets preamble, explicit header, payload length, CRC on, normal IQ (SetPacketParams 0x8C).
static bool set_packet_params(uint8_t payload_len)
{
    uint8_t p[6] = {
        LORA_PREAMBLE_LEN >> 8, LORA_PREAMBLE_LEN & 0xFF,
        0x00,                       // explicit header
        payload_len,                // TX: exact length, RX: maximum length
        0x01,                       // CRC on
        0x00,                       // standard IQ
    };
    return cmd(0x8C, p, 6);                                    // SetPacketParams
}

// Applies all radio settings in the order the datasheet requires:
// standby -> TCXO -> regulator -> calibrate -> RF switch -> LoRa mode -> image
// calibration -> frequency -> PA + power -> buffer -> SF/BW/CR -> sync word -> interrupts.
static bool configure(void)
{
    uint8_t p[8];
    bool ok = true;

    p[0] = 0x00; ok &= cmd(0x80, p, 1);                        // SetStandby(RC)
    if (LORA_USE_TCXO) {
        p[0] = LORA_TCXO_VOLTAGE; p[1] = 0x00; p[2] = 0x01; p[3] = 0x40;   // 5 ms start-up
        ok &= cmd(0x97, p, 4);                                 // SetDIO3AsTCXOCtrl
    }
    p[0] = LORA_USE_DCDC ? 0x01 : 0x00; ok &= cmd(0x96, p, 1); // SetRegulatorMode
    p[0] = 0x7F; ok &= cmd(0x89, p, 1);                        // Calibrate (all blocks)
    vTaskDelay(pdMS_TO_TICKS(5));
    if (LORA_USE_DIO2_RF_SWITCH) { p[0] = 0x01; ok &= cmd(0x9D, p, 1); }   // SetDIO2AsRfSwitchCtrl
    p[0] = 0x01; ok &= cmd(0x8A, p, 1);                        // SetPacketType(LoRa)
    calibrate_image();

    uint32_t frf = (uint32_t)(((uint64_t)LORA_FREQUENCY_HZ << 25) / 32000000);
    p[0] = frf >> 24; p[1] = frf >> 16; p[2] = frf >> 8; p[3] = frf;
    ok &= cmd(0x86, p, 4);                                     // SetRfFrequency

    p[0] = 0x04; p[1] = 0x07; p[2] = 0x00; p[3] = 0x01;        // SetPaConfig: SX1262 high-power PA
    ok &= cmd(0x95, p, 4);
    p[0] = (uint8_t)(int8_t)LORA_TX_POWER_DBM; p[1] = 0x04;    // SetTxParams: power, 200 us ramp
    ok &= cmd(0x8E, p, 2);
    p[0] = 0; p[1] = 0; ok &= cmd(0x8F, p, 2);                 // SetBufferBaseAddress(0, 0)

    // Low-data-rate optimisation is required when one symbol is longer than 16 ms.
    const uint32_t bw_khz = LORA_BANDWIDTH_KHZ > 0 ? LORA_BANDWIDTH_KHZ : 1;   // 0 = not set (checked earlier)
    uint32_t symbol_us = (1000u << LORA_SPREADING_FACTOR) / bw_khz;
    p[0] = LORA_SPREADING_FACTOR; p[1] = bandwidth_code(); p[2] = LORA_CODING_RATE - 4;
    p[3] = symbol_us > 16000 ? 1 : 0;
    ok &= cmd(0x8B, p, 4);                                     // SetModulationParams

    uint8_t sync[2] = {LORA_SYNC_WORD >> 8, LORA_SYNC_WORD & 0xFF};
    ok &= write_register(0x0740, sync, 2);                     // LoRa sync word

    // All interrupts we use on DIO1: TxDone, RxDone, HeaderErr, CrcErr, Timeout
    p[0] = IRQ_ALL >> 8; p[1] = IRQ_ALL & 0xFF; p[2] = IRQ_ALL >> 8; p[3] = IRQ_ALL & 0xFF;
    p[4] = 0; p[5] = 0; p[6] = 0; p[7] = 0;
    ok &= cmd(0x08, p, 8);                                     // SetDioIrqParams
    return ok;
}

// Clears all interrupt flags in the radio.
static void clear_irq(void)
{
    uint8_t clr[2] = {0x03, 0xFF};
    cmd(0x02, clr, 2);                                         // ClearIrqStatus
}

// Waits until DIO1 goes high (an interrupt happened) or the time runs out.
// Returns the radio's IRQ flags, or 0 on timeout.
static uint16_t wait_irq(uint32_t timeout_ms)
{
    int64_t end = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    while (esp_timer_get_time() < end) {
        if (gpio_get_level(PIN_LORA_DIO1)) return get_irq_status();
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    return 0;
}

// Puts the radio in standby (SetStandby 0x80).
static void standby(void)
{
    uint8_t stby = 0x00;
    cmd(0x80, &stby, 1);
}

// ---------------------------------------------------------------- public
// See lora.h. The radio's power comes from the sensor rail, so the rail must be on.
bool lora_spi_check(void)
{
    if (!bus_start()) return false;
    bool ok = reset_radio();
    uint8_t sync[2] = {0};
    if (ok) ok = read_register(0x0740, sync, 2);
    bus_stop();
    ESP_LOGI(TAG, "SPI check: sync word register = 0x%02X%02X (reset value 0x1424) -> %s",
             sync[0], sync[1], (ok && sync[0] == 0x14 && sync[1] == 0x24) ? "OK" : "FAIL");
    return ok && sync[0] == 0x14 && sync[1] == 0x24;
}

// See lora.h: config check -> SPI bus -> reset -> configure().
bool lora_begin(void)
{
    if (!lora_config_ready()) return false;
    if (bandwidth_code() == 0xFF) {
        ESP_LOGE(TAG, "LORA_BANDWIDTH_KHZ must be 125, 250 or 500");
        return false;
    }
    if (!bus_start()) return false;
    if (!reset_radio() || !configure()) {
        ESP_LOGE(TAG, "radio configuration failed");
        bus_stop();
        return false;
    }
    return true;
}

// Copies the packet into the radio buffer (WriteBuffer 0x0E), sets its length,
// starts transmission (SetTx 0x83) and waits for the TX-done interrupt.
bool lora_transmit(const uint8_t *data, size_t len)
{
    if (len == 0 || len > LORA_MAX_PACKET) return false;
    uint8_t buf[2 + LORA_MAX_PACKET] = {0x0E, 0x00};          // WriteBuffer at offset 0
    memcpy(&buf[2], data, len);
    uint8_t tx[3] = {0, 0, 0};                                 // SetTx without radio timeout
    bool ok = set_packet_params((uint8_t)len) && transfer(buf, NULL, len + 2);
    clear_irq();
    ok = ok && cmd(0x83, tx, 3);
    uint16_t irq = ok ? wait_irq(LORA_TX_TIMEOUT_MS) : 0;
    clear_irq();
    if (!(irq & IRQ_TX_DONE)) {
        standby();
        ESP_LOGE(TAG, "transmission failed or timed out");
        return false;
    }
    return true;                                               // radio returns to standby by itself
}

// Starts receiving (SetRx 0x82) with a radio timeout, waits for RX-done,
// then asks where the packet is (GetRxBufferStatus 0x13) and reads it (ReadBuffer 0x1E).
int lora_receive(uint8_t *buf, size_t max, uint32_t timeout_ms)
{
    uint32_t units = timeout_ms * 64;                          // radio timeout unit = 15.625 us
    uint8_t rx[3] = {units >> 16, units >> 8, units};
    clear_irq();
    if (!set_packet_params(LORA_MAX_PACKET) || !cmd(0x82, rx, 3)) return -1;   // SetRx
    uint16_t irq = wait_irq(timeout_ms + 50);                  // small margin over the radio timeout
    clear_irq();
    if (!(irq & IRQ_RX_DONE) || (irq & (IRQ_CRC_ERR | IRQ_HEADER_ERR))) {
        standby();
        return -1;                                             // timeout or damaged packet
    }
    uint8_t q[4] = {0x13, 0, 0, 0}, r[4] = {0};                // GetRxBufferStatus
    if (!transfer(q, r, 4)) return -1;
    uint8_t len = r[2], start = r[3];
    if (len > max) len = (uint8_t)max;
    uint8_t tx[3 + LORA_MAX_PACKET] = {0x1E, start, 0}, data[3 + LORA_MAX_PACKET];   // ReadBuffer
    if (!transfer(tx, data, 3 + len)) return -1;
    memcpy(buf, &data[3], len);
    return len;
}

// Standby + release SPI. Safe to call if lora_begin() failed.
void lora_end(void)
{
    if (!s_spi) return;
    standby();
    bus_stop();
}

// One packet in one call (used for the telemetry in HYBRID mode).
bool lora_send(const uint8_t *data, size_t len)
{
    if (!lora_begin()) return false;
    bool ok = lora_transmit(data, len);
    lora_end();
    if (ok) ESP_LOGI(TAG, "sent %u bytes", (unsigned)len);
    return ok;
}
