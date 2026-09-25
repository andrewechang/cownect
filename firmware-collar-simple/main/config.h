// config.h - board-wide settings: device ID, cycle timing, send mode and the pin map.
//
// Settings that belong to one sensor or module are in that module's own .h file:
//   accel.h        accelerometer sample rate, range, FIFO polling
//   gps.h          GPS UART port, baud rate
//   analog.h       microphone sample rate, temperature averaging, sensor constants
//   lora.h         LoRa radio settings (frequency, SF, power, ...)
//   lora_full.h    LORA_FULL fragment size, ACK timeout, retries
//   wifi_upload.h  Wi-Fi name/password, Jetson IP/port, timeouts
//
// Values marked "TODO" are NOT known yet. They are left at a "not set" value on
// purpose: the part that needs them is skipped with a log message instead of
// running with a guessed value.
#pragma once

// ---------------------------------------------------------------- identity
#define DEVICE_ID              1u      // development default, one number per collar

// ---------------------------------------------------------------- cycle timing
#define CYCLE_MS               120000  // one full cycle: wake, capture, send, sleep
#define CAPTURE_MS             30000   // sensor recording window
#define RAIL_STABILIZE_MS      100     // wait after switching the sensor rail on (development default)

// For bench testing with the USB cable: 1 = wait with a normal delay instead of
// deep sleep, so the serial monitor stays connected. Use 0 on the real collar.
#define DEBUG_NO_DEEP_SLEEP    0

// ---------------------------------------------------------------- how the data is sent
// COMM_HYBRID    : 49-byte summary over LoRa + raw capture over Wi-Fi to the Jetson (main path)
// COMM_LORA_FULL : the whole raw capture over LoRa in fragments (research comparison, very slow)
#define COMM_HYBRID            1
#define COMM_LORA_FULL         2
#define COMM_MODE              COMM_HYBRID

// ---------------------------------------------------------------- pins (schematic 2026-09-06)
// These follow the PCB wiring; change them only if the hardware changes.
#define PIN_BOARD_TEMP_ADC     1       // ADC1_CH0, MCP9700
#define PIN_COW_TEMP_ADC       2       // ADC1_CH1, MF58 thermistor divider
#define PIN_PERIPH_EN          4       // HIGH = sensor power rail (SWITCHED_3V3) on
#define PIN_MIC_ADC            5       // ADC1_CH4, microphone amplifier
#define PIN_I2C_SDA            6
#define PIN_I2C_SCL            7
#define PIN_LORA_CS            9
#define PIN_LORA_MOSI          10
#define PIN_LORA_SCK           11
#define PIN_LORA_MISO          12
#define PIN_LORA_RST           13
#define PIN_LORA_DIO1          14
#define PIN_MODE_SW            15      // PROG switch: LOW = programming (idle), HIGH = normal
#define PIN_GPS_RX             17      // ESP32 RX  <- GPS TXD
#define PIN_GPS_TX             18      // ESP32 TX  -> GPS RXD
#define PIN_LORA_BUSY          21
