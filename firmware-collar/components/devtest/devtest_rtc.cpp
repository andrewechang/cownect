#include "devtest_rtc.h"

#include <cstdio>
#include "cycle_scheduler.h"
#include "devtest.h"
#include "devtest_common.h"
#include "esp_attr.h"
#include "esp_sleep.h"
#include "rtc_state.h"
#include "sleep_manager.h"

namespace {
constexpr uint32_t kMagic = 0x44545354;  // "DTST"
enum Kind : uint32_t { NONE = 0, SLEEP_TEST = 1, CYCLE_TEST = 2 };

struct DevTestRtc {
    uint32_t magic;
    uint32_t kind;
    uint32_t cycle_ms;
    uint32_t capture_ms;
    uint32_t remaining;
    uint32_t sleep_s;
};
RTC_DATA_ATTR DevTestRtc s_dt;
}  // namespace

void devtest_rtc_arm_sleep_test(uint32_t sleep_s)
{
    s_dt = {kMagic, SLEEP_TEST, 0, 0, 0, sleep_s};
}

void devtest_rtc_arm_cycles(uint32_t cycle_ms, uint32_t capture_ms, uint32_t remaining_after_current)
{
    s_dt = {kMagic, CYCLE_TEST, cycle_ms, capture_ms, remaining_after_current, 0};
}

void devtest_rtc_clear()
{
    s_dt = {};
}

DevTestRtcKind devtest_rtc_pending()
{
    if (s_dt.magic != kMagic) {
        return DevTestRtcKind::NONE;
    }
    return s_dt.kind == SLEEP_TEST   ? DevTestRtcKind::SLEEP_TEST
           : s_dt.kind == CYCLE_TEST ? DevTestRtcKind::CYCLE_TEST
                                     : DevTestRtcKind::NONE;
}

bool devtest_resume_after_wake()
{
    if (devtest_rtc_pending() == DevTestRtcKind::NONE) {
        return false;
    }
    devtest_rtc_resume();
    return true;
}

int devtest_rtc_resume()
{
    if (devtest_rtc_pending() == DevTestRtcKind::NONE) {
        return 0;
    }
    using namespace cownect;
    const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    const data::RtcSchedulerState& rtc = data::rtc_state();
    std::printf("[devtest] resumed after wake: cause=%s cycle_seq=%u next_capture_id=%u last_sleep_req_ms=%u\n",
                power::wake_cause_name(cause), static_cast<unsigned>(rtc.cycle_sequence),
                static_cast<unsigned>(rtc.next_capture_id), static_cast<unsigned>(rtc.last_sleep_requested_ms));
    if (cause != ESP_SLEEP_WAKEUP_TIMER) {
        std::printf("[devtest] wake was not a timer wake - test context discarded\n");
        devtest::report_fail("sleep", "unexpected wake/reset cause (expected a timer wake)");
        devtest_rtc_clear();
        return 1;
    }
    if (s_dt.kind == SLEEP_TEST) {
        devtest_rtc_clear();
        devtest::report_pass("sleep", "timer wake, retained RTC state valid");
        devtest::report_hw("sleep",
                           "confirm the board really slept (current meter / USB disconnect) for ~the requested time; "
                           "sleep time is requested, not measured");
        return 0;
    }
    // CYCLE_TEST: continue the remaining development cycles (deep sleep allowed in NORMAL).
    scheduler::SchedulerConfig cfg = scheduler::scheduler_production_config();
    cfg.cycle_target_ms = s_dt.cycle_ms;
    cfg.capture_ms = s_dt.capture_ms;
    cfg.allow_deep_sleep = true;
    cfg.wait_until_boundary = true;
    scheduler::cycle_scheduler().init(cfg);
    while (s_dt.remaining > 0) {
        s_dt.remaining--;
        scheduler::cycle_scheduler().run_one_cycle();  // may deep sleep and resume here via boot
    }
    devtest_rtc_clear();
    devtest::report_pass("cycle_repeat", "all requested cycles executed");
    devtest::report_hw("cycle_repeat", "check wake period with an external timer/current logger");
    return 0;
}
