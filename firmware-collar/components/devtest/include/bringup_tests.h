#pragma once

// CowNect PCB bring-up tests, called from main/app_main.cpp (one uncommented call at a time).
//
// Each function:
//   * prints a start banner "MATERIAL n TEST Mn.k: <title>",
//   * initializes every subsystem it needs itself (no earlier test has to run first),
//   * calls the REAL production drivers / managers (no second fake implementation),
//   * obeys every CONFIG_NOT_SET / RF TX gate (never substitutes guessed values),
//   * ends with "Mn.k RESULT: PASS | FAIL | BLOCKED | NEEDS_HARDWARE_VALIDATION" plus a Reason
//     or the operator checks.
//
// The implementations live next to the subsystem they exercise, in the per-Material files of
// components/devtest (test_board.cpp, test_accel.cpp, ... test_hybrid.cpp). The same test
// bodies are also reachable from the development console (`test <name>`).
//
// Tests marked [DEEP SLEEP] may put the chip into timer deep sleep. After the timer wake the
// chip reboots, app_main calls the same test again and it reports the result instead of
// sleeping again. Run those tests alone.
// Tests marked [RF TX] print "VERIFY EXTERNAL ANTENNA IS CONNECTED BEFORE RF TX" and transmit
// only when the RF profile is configured and COWNECT_LORA_ANTENNA_VERIFIED is set.

// ---- Material 4: base system / board ---------------------------------------------------
void test_boot_report();          // M4.1 FW/IDF version, reset + wake cause, RTC state, mode
void test_psram();                // M4.2 PSRAM self-test + microphone capture buffer
void test_board_gpio();           // M4.3 pin map, BOOT / MODE_SW / PERIPH_EN levels (15 s watch)
void test_operation_mode();       // M4.4 debounced PROG switch (20 s, move the switch)
void test_peripheral_enable();    // M4.5 PERIPH_EN ON 10 s -> OFF 10 s (measure SWITCHED_3V3)
void test_adc_smoke();            // M4.6 raw GPIO1 / GPIO2 / GPIO5 ADC reads
void test_config_status();        // M4.7 CONFIG_NOT_SET overview

// ---- Material 5: LIS2DW12 accelerometer -------------------------------------------------
void test_accelerometer();              // M5.1 WHO_AM_I, register readback, 30 s FIFO capture
void test_accelerometer_overrun();      // M5.2 deliberate FIFO overrun + recovery
void test_accelerometer_repeat();       // M5.3 3 x 5 s captures
void test_accelerometer_power_cycle();  // M5.4 rail off/on re-initialization
void test_accelerometer_decode();       // M5.5 raw->g decode math (no hardware)

// ---- Material 6: temperature ------------------------------------------------------------
void test_temperature();          // M6.1 MCP9700 + MF58, 1 Hz one-shot, 30 s
void test_mcp9700();              // M6.2 board sensor only, 10 s
void test_thermistor();           // M6.3 MF58 cow probe only, 10 s (unplug/short checks)
void test_adc_conflict();         // M6.4 one-shot refused while continuous ADC1 is owned
void test_temperature_math();     // M6.5 conversion math (no hardware)

// ---- Material 7: GPS --------------------------------------------------------------------
void test_gps();                  // M7.1 UART / NMEA traffic, 10 s
void test_gps_discovery();        // M7.2 sentence identifiers, 20 s
void test_gps_capture();          // M7.3 fix retention, 30 s
void test_gps_power_cycle();      // M7.4 VCC off/on, VBAT retained, up to 60 s
void test_nmea_parser();          // M7.5 NMEA framer/parser (no hardware)

// ---- Material 8: microphone -------------------------------------------------------------
void test_microphone();           // M8.1 standalone 32 kS/s capture, 30 s
void test_mic_stream();           // M8.2 ADC stream / channel identification, 3 s
void test_mic_overflow();         // M8.3 deliberate slow consumer + recovery
void test_mic_power_cycle();      // M8.4 rail off/on re-initialization
void test_mic_dump();             // M8.5 1 s capture, first/last 32 raw samples

// ---- Material 9: integrated capture -----------------------------------------------------
void test_analog_adc_engine();    // M9.1 AnalogAdcEngine MIC,TB,MIC,TC pattern, 5 s
void test_sensor_manager();       // M9.2 SensorManager short integrated capture, 5 s
void test_capture();              // M9.3 full production-length capture (CAPTURE_TIME_MS)
void test_capture_repeat();       // M9.4 3 x 5 s captures
void test_capture_power_cycle();  // M9.5 capture, rail off/on, capture

// ---- Material 10: scheduler / power / sleep --------------------------------------------
void test_scheduler();                   // M10.1 CycleScheduler awake cycle 20 s / 5 s
void test_power_manager();               // M10.2 capture then PERIPH_EN commanded OFF
void test_sleep_manager();               // M10.3 [DEEP SLEEP] 10 s timer deep sleep (NORMAL mode)
void test_scheduler_cycle();             // M10.4 [DEEP SLEEP] 20 s / 5 s cycle, sleeps in NORMAL
void test_scheduler_repeat();            // M10.5 [DEEP SLEEP] 3 cycles across deep sleep
void test_scheduler_overrun();           // M10.6 forced overrun: sleep=0, overrun recorded
void test_scheduler_production_cycle();  // M10.7 [DEEP SLEEP] one 120 s / 30 s cycle

// ---- Material 11: features / telemetry --------------------------------------------------
void test_feature_processor();    // M11.1 SimpleFeatureProcessor on a fixture (no hardware)
void test_telemetry();            // M11.2 real 5 s capture -> TelemetryRecord

// ---- Material 12: SX1262 / LoRa ---------------------------------------------------------
void test_lora_spi();             // M12.1 SPI GetStatus after power-up (non-radiating)
void test_lora_reset();           // M12.2 NRESET pulse + STDBY_RC (non-radiating)
void test_lora_busy();            // M12.3 BUSY handshake + SetStandby (non-radiating)
void test_lora_configuration();   // M12.4 RF profile / TX gate, apply profile (non-radiating)
void test_lora_tx();              // M12.5 [RF TX] 10 PING packets
void test_lora_rx();              // M12.6 30 s receive window (needs RF profile)
void test_lora_ping();            // M12.7 [RF TX] PING/ACK round trip
void test_lora_power_cycle();     // M12.8 rail off/on radio re-init (non-radiating)

// ---- Material 13: serialization / LORA_FULL ---------------------------------------------
void test_serialization();        // M13.1 CaptureStreamEncoder round trip (no hardware)
void test_crc32();                // M13.2 CRC-32/ISO-HDLC check value (no hardware)
void test_fragmentation();        // M13.3 fragment reconstruction (no hardware)
void test_packet_codecs();        // M13.4 telemetry / test / ACK / upload header codecs
void test_lora_full_small();      // M13.5 [RF TX] 4096 B synthetic stream transfer
void test_lora_full();            // M13.6 [RF TX] real 30 s capture, full transfer
void test_lora_full_loss();       // M13.7 [RF TX] deliberate fragment loss
void test_lora_full_overrun();    // M13.8 [RF TX] transfer beyond a 20 s cycle

// ---- Material 14: Wi-Fi / TCP / upload --------------------------------------------------
void test_wifi_connect();         // M14.1 join configured AP
void test_wifi_tcp();             // M14.2 17-byte TCP message to the Jetson
void test_wifi_small_upload();    // M14.3 64 KiB generated stream upload
void test_wifi_capture_upload();  // M14.4 real 30 s capture upload
void test_wifi_repeat();          // M14.5 3 x 5 s capture uploads

// ---- Material 15: hybrid communication --------------------------------------------------
void test_hybrid_short();         // M15.1 [RF TX] 5 s capture -> LoRa telemetry -> Wi-Fi raw
void test_hybrid_full();          // M15.2 [RF TX] 30 s capture -> LoRa telemetry -> Wi-Fi raw
void test_hybrid_repeat();        // M15.3 [RF TX] 3 short hybrid cycles

// ---- Bring-up tools ---------------------------------------------------------------------
void test_unit_all();             // every pure-function group (no PCB peripherals needed)
void test_start_console();        // development console (`test <name>` commands); returns
void bringup_print_summary();     // totals of the tests run, then commands the rail OFF
