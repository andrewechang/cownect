// config.h - board-wide settings: device ID, cycle timing, send mode and pins.
//
// Each sensor/module keeps its own settings at the top of its .h file:
//   accel.h, gps.h, analog.h, features.h, lora.h, wifi_upload.h
//
// "TODO" values are not known yet. They stay "not set" on purpose: the part that
// needs them is skipped with a log message instead of running on a guess.
#pragma once

// ---------------------------------------------------------------- identity
#define DEVICE_ID              1u      // development default, one number per collar

// ---------------------------------------------------------------- cycle timing
#define CYCLE_MS               60000   // one full cycle: wake, record, send, sleep
#define CAPTURE_MS             15000   // how long the sensors record
#define RAIL_STABILIZE_MS      3000    // wait after switching the sensor power on: the mic amplifier bias needs ~3 s to settle

// ---------------------------------------------------------------- GPS first fix
// A GPS with no saved satellite data needs minutes of continuous power for its first fix; the
// ~18 s per cycle is not enough. VBAT keeps the data through deep sleep afterwards.
// So the GPS stays on until its first fix before the normal cycle starts:
#define GPS_FIRST_FIX_MAX_MS     (15 * 60 * 1000)  // at power-up / reset: wait at most this long
#define GPS_EXTRA_ON_MS          (3 * 60 * 1000)   // after the fix: stay on so it stores all satellites
#define GPS_RECOVER_AFTER_CYCLES 10                // this many cycles in a row without a fix ->
#define GPS_RECOVER_MAX_MS       (5 * 60 * 1000)   //   wait again, at most this long
#define GPS_WAIT_STATUS_MS       60000             // LoRa status "GPS waiting, not fix yet" this often

// 1 = wait awake instead of deep sleep, so the USB serial monitor stays connected.
// Use 0 on the real collar.
#define DEBUG_NO_DEEP_SLEEP    0

// ---------------------------------------------------------------- how the data is sent
// COMM_HYBRID    : 49-byte summary over LoRa + full recording over Wi-Fi (Jetson or website, wifi_upload.h)
// COMM_LORA_FULL : LoRa only. ONE packet per cycle with values worked out on the collar
//                  (features.h): 157 bytes, ~0.4 s on air. The raw recording is not sent.
#define COMM_HYBRID            1
#define COMM_LORA_FULL         2
#define COMM_MODE              COMM_LORA_FULL

// ---------------------------------------------------------------- pins (schematic 2026-09-06)
#define PIN_BOARD_TEMP_ADC     1       // ADC1_CH0, MCP9700
#define PIN_COW_TEMP_ADC       2       // ADC1_CH1, MF58 thermistor divider
#define PIN_PERIPH_EN          4       // HIGH = sensor power rail on
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
#define PIN_GPS_RX             39      // ESP32 RX <- GPS TXD
#define PIN_GPS_TX             40      // ESP32 TX -> GPS RXD
#define PIN_LORA_BUSY          21
