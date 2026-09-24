// CowNect collar - application entry and PCB bring-up checklist.
//
// HOW TO USE
//   1. Uncomment ONE test call below (work down from Material 4 to Material 15).
//   2. idf.py build flash monitor
//   3. Read the banner in the serial output:
//        MATERIAL n TEST Mn.k: <title>  ...  Mn.k RESULT: PASS | FAIL | BLOCKED | NEEDS_HARDWARE_VALIDATION
//   4. Record the result in BUILD_AND_VALIDATION_REPORT.md, comment the call again, move on.
//
// With everything commented (the default) the firmware only performs the safe base init
// (PERIPH_EN commanded OFF) and then idles: no sensors, no Wi-Fi, no LoRa TX, no scheduler.
//
// Every test initializes what it needs itself, so no earlier test has to be uncommented first.
// The test bodies live in components/devtest (one file per Material) and call the real drivers;
// they are also available from the development console (`test <name>`).
//
// [DEEP SLEEP] tests reboot through a timer wake and report their result on the second pass:
//              run them alone.
// [RF TX]      tests print "VERIFY EXTERNAL ANTENNA IS CONNECTED BEFORE RF TX" and transmit
//              only when the RF profile is configured and COWNECT_LORA_ANTENNA_VERIFIED is set.

#include "bringup_tests.h"
#include "cownect_app.h"

extern "C" void app_main(void)
{
    // Safe base init (always): MODE_SW, PERIPH_EN commanded OFF, RTC state, PSRAM capture buffer.
    cownect_system_init();

    // ============================================================
    // MATERIAL 4 — BASE SYSTEM / BOARD
    // ============================================================

    // test_boot_report();                  // M4.1 FW/IDF version, reset + wake cause, RTC state, mode
    // test_psram();                        // M4.2 PSRAM self-test + capture buffer
    // test_board_gpio();                   // M4.3 pin map, BOOT / MODE_SW levels (press / move them)
    // test_operation_mode();               // M4.4 debounced PROG switch (move it during 20 s)
    // test_peripheral_enable();            // M4.5 PERIPH_EN ON 10 s / OFF 10 s (measure SWITCHED_3V3)
    // test_adc_smoke();                    // M4.6 raw GPIO1 / GPIO2 / GPIO5 reads
    // test_config_status();                // M4.7 CONFIG_NOT_SET overview

    // ============================================================
    // MATERIAL 5 — LIS2DW12 ACCELEROMETER
    // ============================================================

    // test_accelerometer();                // M5.1 WHO_AM_I, register readback, 30 s FIFO capture
    // test_accelerometer_overrun();        // M5.2 deliberate FIFO overrun + recovery
    // test_accelerometer_repeat();         // M5.3 3 x 5 s captures
    // test_accelerometer_power_cycle();    // M5.4 rail off/on re-init
    // test_accelerometer_decode();         // M5.5 decode math (no hardware)

    // ============================================================
    // MATERIAL 6 — TEMPERATURE
    // ============================================================

    // test_temperature();                  // M6.1 MCP9700 + MF58 at 1 Hz, 30 s
    // test_mcp9700();                      // M6.2 board sensor only
    // test_thermistor();                   // M6.3 MF58 cow probe only (unplug / short checks)
    // test_adc_conflict();                 // M6.4 ADC1 ownership conflict refused
    // test_temperature_math();             // M6.5 conversion math (no hardware)

    // ============================================================
    // MATERIAL 7 — GPS
    // ============================================================

    // test_gps();                          // M7.1 UART / NMEA traffic
    // test_gps_discovery();                // M7.2 NMEA sentence identifiers
    // test_gps_capture();                  // M7.3 fix retention (outdoor for a fix)
    // test_gps_power_cycle();              // M7.4 VCC off/on, VBAT retained
    // test_nmea_parser();                  // M7.5 NMEA parser (no hardware)

    // ============================================================
    // MATERIAL 8 — MICROPHONE
    // ============================================================

    // test_microphone();                   // M8.1 standalone 32 kS/s capture, 30 s
    // test_mic_stream();                   // M8.2 ADC stream / channel identification
    // test_mic_overflow();                 // M8.3 deliberate slow consumer + recovery
    // test_mic_power_cycle();              // M8.4 rail off/on re-init
    // test_mic_dump();                     // M8.5 raw sample preview

    // ============================================================
    // MATERIAL 9 — INTEGRATED CAPTURE
    // ============================================================

    // test_analog_adc_engine();            // M9.1 AnalogAdcEngine MIC,TB,MIC,TC pattern
    // test_sensor_manager();               // M9.2 SensorManager 5 s integrated capture
    // test_capture();                      // M9.3 full production-length capture
    // test_capture_repeat();               // M9.4 3 x 5 s captures
    // test_capture_power_cycle();          // M9.5 capture, rail off/on, capture

    // ============================================================
    // MATERIAL 10 — SCHEDULER / POWER
    // ============================================================

    // test_scheduler();                    // M10.1 CycleScheduler awake cycle 20 s / 5 s
    // test_power_manager();                // M10.2 capture then PERIPH_EN commanded OFF
    // test_sleep_manager();                // M10.3 [DEEP SLEEP] 10 s timer sleep (PROG = NORMAL)
    // test_scheduler_cycle();              // M10.4 [DEEP SLEEP] 20 s / 5 s cycle
    // test_scheduler_repeat();             // M10.5 [DEEP SLEEP] 3 cycles
    // test_scheduler_overrun();            // M10.6 forced overrun
    // test_scheduler_production_cycle();   // M10.7 [DEEP SLEEP] one 120 s / 30 s cycle

    // ============================================================
    // MATERIAL 11 — FEATURES / TELEMETRY
    // ============================================================

    // test_feature_processor();            // M11.1 feature math on a fixture (no hardware)
    // test_telemetry();                    // M11.2 real capture -> TelemetryRecord

    // ============================================================
    // MATERIAL 12 — SX1262 / LORA
    // ============================================================

    // test_lora_spi();                     // M12.1 SPI GetStatus (non-radiating)
    // test_lora_reset();                   // M12.2 NRESET -> STDBY_RC (non-radiating)
    // test_lora_busy();                    // M12.3 BUSY handshake + standby (non-radiating)
    // test_lora_configuration();           // M12.4 RF profile / TX gate, apply profile (non-radiating)
    // test_lora_tx();                      // M12.5 [RF TX] 10 PING packets
    // test_lora_rx();                      // M12.6 30 s receive window
    // test_lora_ping();                    // M12.7 [RF TX] PING / ACK round trip
    // test_lora_power_cycle();             // M12.8 rail off/on radio re-init (non-radiating)

    // ============================================================
    // MATERIAL 13 — SERIALIZATION / FULL LORA TRANSFER
    // ============================================================

    // test_serialization();                // M13.1 capture stream round trip (no hardware)
    // test_crc32();                        // M13.2 CRC-32 check value (no hardware)
    // test_fragmentation();                // M13.3 fragment reconstruction (no hardware)
    // test_packet_codecs();                // M13.4 packet encoders / decoders (no hardware)
    // test_lora_full_small();              // M13.5 [RF TX] 4096 B synthetic stream
    // test_lora_full();                    // M13.6 [RF TX] real 30 s capture
    // test_lora_full_loss();               // M13.7 [RF TX] deliberate fragment loss
    // test_lora_full_overrun();            // M13.8 [RF TX] transfer beyond a 20 s cycle

    // ============================================================
    // MATERIAL 14 — WIFI / TCP / UPLOAD
    // ============================================================

    // test_wifi_connect();                 // M14.1 join the configured AP
    // test_wifi_tcp();                     // M14.2 17-byte message to the Jetson
    // test_wifi_small_upload();            // M14.3 64 KiB generated stream
    // test_wifi_capture_upload();          // M14.4 real 30 s capture upload
    // test_wifi_repeat();                  // M14.5 3 x 5 s capture uploads

    // ============================================================
    // MATERIAL 15 — HYBRID COMMUNICATION
    // ============================================================

    // test_hybrid_short();                 // M15.1 [RF TX] 5 s capture -> LoRa telemetry -> Wi-Fi raw
    // test_hybrid_full();                  // M15.2 [RF TX] full capture -> LoRa telemetry -> Wi-Fi raw
    // test_hybrid_repeat();                // M15.3 [RF TX] 3 short hybrid cycles

    // ============================================================
    // BRING-UP TOOLS (optional)
    // ============================================================

    // test_unit_all();                     // U.1 every pure-function test (no PCB peripherals)
    // test_start_console();                // development console: `test <name>` commands

    // Totals of the tests above, then SWITCHED_3V3 commanded OFF. Harmless when nothing ran.
    bringup_print_summary();

    // ============================================================
    // FINAL NORMAL FIRMWARE
    // ============================================================

    // run_cownect_firmware();
}
