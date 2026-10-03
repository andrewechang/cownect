// test_mode.h - hardware bring-up tests, one module at a time.
//
// Instead of the normal 120 s cycle, the firmware runs ONE test over and over
// (or all tests once, in order) and prints raw readings on the serial monitor
// (idf.py -p COMx monitor, 115200 baud). No deep sleep in test mode.
//
// Recommended order (each test needs the ones before it to pass):
//   TEST 1  ESP32 chip     : chip model, flash, PSRAM, memory, reset reason, MAC
//   TEST 2  Power + switch : sensor rail on/off (measure it), PROG switch level
//   TEST 3  Accelerometer  : chip ID, X/Y/Z in g, |a| (should be ~1.0 g lying still)
//   TEST 4  Microphone     : sample count, min/max/RMS (RMS rises when you clap)
//   TEST 5  Temperatures   : board + cow probe, raw / mV / ohm / C every second
//   TEST 6  GPS            : every NMEA sentence as received + parsed position
//   TEST 7  LoRa SPI       : wiring check (sync word register = 0x1424), no transmission
//   TEST 8  LoRa transmit  : sends one 49-byte telemetry packet (needs lora.h values + antenna)
//   TEST 9  Wi-Fi          : connects, shows IP + signal, opens a TCP connection to the Jetson
//                            (needs wifi_upload.h values)
//
// To use: set TEST_NUMBER below, build, flash, open the monitor.
// Set TEST_NUMBER back to 0 for the real collar firmware.
#pragma once

// ================================================================ SETTINGS
#define TEST_ALL               99      // special value: run tests 1..9 once each, then a PASS/FAIL list

#define TEST_NUMBER            TEST_ALL       // 0 = normal firmware (no tests)
                                       // 1..9 = repeat that one test forever
                                       // TEST_ALL = run every test once, in order

#define TEST_REPEAT_MS         2000    // pause between two runs of the same test
#define TEST_LORA_TX_EVERY_MS  60000   // TEST 8 only: pause between packets. Keep it long so the
                                       // radio stays within your region's duty-cycle limit.
// ================================================================

#include "capture.h"

// Runs the test(s) chosen by TEST_NUMBER. Never returns.
// `c` is the firmware's capture buffer, reused here for the sensor readings.
void test_run(capture_t *c);
