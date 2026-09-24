#pragma once

#include <cstdint>

// RTC-retained context so development deep-sleep tests can continue after a timer wake.
void devtest_rtc_arm_sleep_test(uint32_t sleep_s);
void devtest_rtc_arm_cycles(uint32_t cycle_ms, uint32_t capture_ms, uint32_t remaining_after_current);
void devtest_rtc_clear();

// Which deep-sleep test (if any) was in progress before this boot.
enum class DevTestRtcKind : uint32_t { NONE = 0, SLEEP_TEST = 1, CYCLE_TEST = 2 };
DevTestRtcKind devtest_rtc_pending();

// Finishes / continues the pending context (may deep sleep again for remaining cycles).
// Returns 0 = PASS, 1 = FAIL (e.g. the wake was not a timer wake). Clears the context when done.
int devtest_rtc_resume();
