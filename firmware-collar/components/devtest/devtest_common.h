#pragma once

#include <cstdint>
#include <initializer_list>
#include "esp_err.h"

namespace cownect::devtest {

// Every hardware test prints two separate lines:
//   SOFTWARE_RESULT: what firmware could check by itself (PASS/FAIL/BLOCKED)
//   HARDWARE_VALIDATION: NEEDS_HARDWARE_VALIDATION + what the operator must confirm
// Firmware never claims a hardware PASS from code alone.
// report_fail / report_blocked / report_hw also remember their detail text so the app_main
// bring-up banner can print it as the final "Reason:" (no silent failures).
void report_pass(const char* test, const char* detail = "");
void report_fail(const char* test, const char* detail = "");
void report_blocked(const char* test, esp_err_t err, const char* detail = "");
void report_hw(const char* test, const char* operator_checks);

uint32_t arg_u32(int argc, char** argv, int index, uint32_t default_value);

// Commands SWITCHED_3V3 on through the power manager (+ centralized stabilization delay).
esp_err_t rail_on();
// Commands the rail off and tells sensors/radio that their state is lost.
void rail_off();

// Refuses to run hardware tests while a capture is active.
bool ensure_idle(const char* test);

// ---- app_main bring-up framing (bringup_tests.h) ----------------------------------------
// Result of one bring-up test as printed in the final banner.
//   Judge::SOFTWARE : firmware can decide PASS/FAIL itself (hardware notes still printed)
//   Judge::OPERATOR : firmware can only exercise the hardware; rc==0 prints
//                     RESULT: NEEDS_HARDWARE_VALIDATION instead of PASS
enum class Judge { SOFTWARE, OPERATOR };

// Prints the start banner and clears the remembered reasons. rf_tx adds the antenna warning.
void begin(int material, const char* id, const char* title, bool rf_tx = false);
// Classifies a console-style return code (0 = PASS, 1 = FAIL, 2 = BLOCKED) and prints the
// result banner with the remembered reason / operator checks.
void finish(const char* id, int rc, Judge judge = Judge::SOFTWARE);

// Calls a console test entry with literal arguments, e.g. call(cmd_accel, {"accel", "30"}).
int call(int (*fn)(int, char**), std::initializer_list<const char*> args);

// Prints the external-antenna warning. Used by every test that may transmit RF.
void rf_tx_warning();
// Called by a transmitting test once every RF gate has passed, right before the first TX.
void rf_tx_countdown(uint32_t seconds = 3);

// Totals of every finish() since boot; then commands the switched rail OFF (safe idle).
void print_summary();

}  // namespace cownect::devtest

// Test entry points (all return 0 = software PASS, 1 = FAIL, 2 = BLOCKED/CONFIG_NOT_SET).
int cmd_board(int argc, char** argv);
int cmd_gpio(int argc, char** argv);
int cmd_mode(int argc, char** argv);
int cmd_rail(int argc, char** argv);
int cmd_adc_smoke(int argc, char** argv);
int cmd_accel(int argc, char** argv);
int cmd_accel_overrun(int argc, char** argv);
int cmd_accel_repeat(int argc, char** argv);
int cmd_accel_power(int argc, char** argv);
int cmd_temperature(int argc, char** argv);
int cmd_temp_board(int argc, char** argv);
int cmd_temp_cow(int argc, char** argv);
int cmd_adc_conflict(int argc, char** argv);
int cmd_gps(int argc, char** argv);
int cmd_gps_discovery(int argc, char** argv);
int cmd_gps_capture(int argc, char** argv);
int cmd_gps_power(int argc, char** argv);
int cmd_microphone(int argc, char** argv);
int cmd_mic_stream(int argc, char** argv);
int cmd_mic_overflow(int argc, char** argv);
int cmd_mic_power(int argc, char** argv);
int cmd_mic_dump(int argc, char** argv);
int cmd_capture(int argc, char** argv);
int cmd_adc_pattern(int argc, char** argv);
int cmd_capture_repeat(int argc, char** argv);
int cmd_capture_power(int argc, char** argv);
int cmd_scheduler(int argc, char** argv);
int cmd_cycle(int argc, char** argv);
int cmd_cycle_repeat(int argc, char** argv);
int cmd_sleep(int argc, char** argv);
int cmd_overrun(int argc, char** argv);
int cmd_cycle_production(int argc, char** argv);
int cmd_power_idle(int argc, char** argv);
int cmd_telemetry(int argc, char** argv);
int cmd_lora_spi(int argc, char** argv);
int cmd_lora_reset(int argc, char** argv);
int cmd_lora(int argc, char** argv);
int cmd_lora_config(int argc, char** argv);
int cmd_lora_tx(int argc, char** argv);
int cmd_lora_rx(int argc, char** argv);
int cmd_lora_ping(int argc, char** argv);
int cmd_lora_power(int argc, char** argv);
int cmd_lora_full(int argc, char** argv);
int cmd_full_serializer(int argc, char** argv);
int cmd_full_fragment(int argc, char** argv);
int cmd_full_small(int argc, char** argv);
int cmd_full_loss(int argc, char** argv);
int cmd_full_overrun(int argc, char** argv);
int cmd_wifi_connect(int argc, char** argv);
int cmd_wifi_tcp(int argc, char** argv);
int cmd_wifi_small_upload(int argc, char** argv);
int cmd_wifi_capture_upload(int argc, char** argv);
int cmd_wifi_repeat(int argc, char** argv);
int cmd_hybrid_short(int argc, char** argv);
int cmd_hybrid_full(int argc, char** argv);
int cmd_hybrid_repeat(int argc, char** argv);
int cmd_unit(int argc, char** argv);
int cmd_status(int argc, char** argv);

// One named pure-function group from test_unit.cpp (accel_decode, temperature_math, nmea,
// crc32, capture_stream, fragmenter, packets, features). Returns true when it passed.
bool unit_run_group(const char* name);
