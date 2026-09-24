#pragma once

// Safe base initialization, idempotent. Runs first in app_main before any test:
//   MODE_SW input, PERIPH_EN output commanded OFF, RTC-retained state, PSRAM capture buffer.
// Starts no sensor, no radio, no Wi-Fi, no scheduler.
void cownect_system_init();

// The finished product (call only after individual hardware bring-up is complete):
//   initialization -> determine operation mode (PROG switch)
//     PROGRAMMING : development console, nothing runs until a `test ...` command
//     NORMAL      : production CycleScheduler forever: power + initialize sensors -> 30 s
//                   capture -> feature processing -> communication per menuconfig mode
//                   -> rail off -> timer deep sleep until the next 120 s boundary
//     UNSTABLE    : stay awake and re-check, never deep sleep
// In NORMAL mode it does not return (deep sleep reboots into app_main).
void run_cownect_firmware();
