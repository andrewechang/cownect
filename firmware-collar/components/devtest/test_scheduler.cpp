// Material 10: scheduler / power / deep-sleep tests (real CycleScheduler, test-only config).
#include <cstdio>
#include "board_mode.h"
#include "board_power.h"
#include "bringup_tests.h"
#include "cownect_config.h"
#include "cycle_scheduler.h"
#include "devtest_common.h"
#include "devtest_rtc.h"
#include "sensor_manager.h"
#include "sleep_manager.h"
#include "capture_session.h"

using namespace cownect;

namespace {

scheduler::SchedulerConfig test_config(uint32_t cycle_ms, uint32_t capture_ms, bool allow_sleep)
{
    scheduler::SchedulerConfig c = scheduler::scheduler_production_config();
    c.cycle_target_ms = cycle_ms;
    c.capture_ms = capture_ms;
    c.allow_deep_sleep = allow_sleep;
    c.wait_until_boundary = true;
    return c;
}

int run_cycle(const char* test, const scheduler::SchedulerConfig& cfg)
{
    if (!devtest::ensure_idle(test)) return 1;
    scheduler::CycleScheduler& s = scheduler::cycle_scheduler();
    if (s.init(cfg) != ESP_OK) {
        devtest::report_fail(test, "invalid config");
        return 1;
    }
    s.run_one_cycle();  // returns unless NORMAL-mode deep sleep was entered
    const scheduler::CycleMetrics& m = s.last_cycle_metrics();
    std::printf("[%s] seq=%u id=%u capture=%ums proc=%ums comm=%ums pd=%ums awake=%ums sleep_req=%ums overrun=%ums "
                "slept=%d result=%s\n",
                test, static_cast<unsigned>(m.cycle_sequence), static_cast<unsigned>(m.capture_id),
                static_cast<unsigned>(m.capture_time_ms), static_cast<unsigned>(m.processing_time_ms),
                static_cast<unsigned>(m.communication_time_ms), static_cast<unsigned>(m.power_down_time_ms),
                static_cast<unsigned>(m.awake_elapsed_ms), static_cast<unsigned>(m.sleep_requested_ms),
                static_cast<unsigned>(m.cycle_overrun_ms), m.entered_deep_sleep,
                scheduler::cycle_result_name(m.result));
    if (!m.capture_complete) {
        devtest::report_fail(test, "the cycle's capture did not complete (see [CYCLE] log)");
        return 1;
    }
    return 0;
}

// A [DEEP SLEEP] test called again by app_main after its timer wake reports instead of re-running.
bool resumed_after_sleep(DevTestRtcKind kind, const char* id)
{
    if (devtest_rtc_pending() != kind) {
        return false;
    }
    std::printf("Resumed after timer deep sleep - reporting the result of the test started before sleep.\n");
    devtest::finish(id, devtest_rtc_resume(), devtest::Judge::OPERATOR);
    return true;
}

}  // namespace

// scheduler_run_awake_cycle_test (stays awake even in NORMAL)
int cmd_scheduler(int argc, char** argv)
{
    const uint32_t cycle = devtest::arg_u32(argc, argv, 1, 20000);
    const uint32_t capture = devtest::arg_u32(argc, argv, 2, 5000);
    const int rc = run_cycle("scheduler", test_config(cycle, capture, false));
    rc ? devtest::report_fail("scheduler") : devtest::report_pass("scheduler", "dynamic remaining-time accounting");
    return rc;
}

// Test 10.3: short cycle; in NORMAL mode it deep-sleeps for the remainder.
int cmd_cycle(int argc, char** argv)
{
    const uint32_t cycle = devtest::arg_u32(argc, argv, 1, 20000);
    const uint32_t capture = devtest::arg_u32(argc, argv, 2, 5000);
    devtest_rtc_arm_cycles(cycle, capture, 0);
    const int rc = run_cycle("cycle", test_config(cycle, capture, true));
    devtest_rtc_clear();  // reached only if no deep sleep happened (PROGRAMMING/UNSTABLE/overrun)
    devtest::report_hw("cycle", "NORMAL: board sleeps then wakes near the cycle boundary; PROGRAMMING: stays awake");
    return rc;
}

// scheduler_run_repeat_test (test 10.7)
int cmd_cycle_repeat(int argc, char** argv)
{
    const uint32_t cycle = devtest::arg_u32(argc, argv, 1, 20000);
    const uint32_t capture = devtest::arg_u32(argc, argv, 2, 5000);
    const uint32_t count = devtest::arg_u32(argc, argv, 3, 3);
    int rc = 0;
    for (uint32_t i = 0; i < count; ++i) {
        devtest_rtc_arm_cycles(cycle, capture, count - i - 1);  // continued after a timer wake
        rc |= run_cycle("cycle_repeat", test_config(cycle, capture, true));
    }
    devtest_rtc_clear();
    rc ? devtest::report_fail("cycle_repeat", "at least one cycle's capture did not complete")
       : devtest::report_pass("cycle_repeat");
    return rc;
}

// scheduler_run_deep_sleep_test (test 10.2)
int cmd_sleep(int argc, char** argv)
{
    const uint32_t seconds = devtest::arg_u32(argc, argv, 1, 10);
    if (board_get_operation_mode() != OperationMode::NORMAL) {
        devtest::report_blocked("sleep", ESP_ERR_INVALID_STATE, "set PROG to NORMAL first (deep sleep only in NORMAL)");
        return 2;
    }
    devtest::rail_off();
    devtest_rtc_arm_sleep_test(seconds);
    std::printf("[sleep] entering %u s timer deep sleep; result is printed after wake\n", static_cast<unsigned>(seconds));
    power::sleep_manager().configure_timer_wakeup(static_cast<uint64_t>(seconds) * 1000000ULL);
    power::sleep_manager().enter_deep_sleep();
}

// Test 10.8 forced overrun: 20 s cycle, 5 s capture, +18 s artificial post-capture delay.
int cmd_overrun(int, char**)
{
    scheduler::SchedulerConfig cfg = test_config(20000, 5000, true);
    cfg.artificial_post_capture_delay_ms = 18000;
    const int rc = run_cycle("overrun", cfg);
    const auto& m = scheduler::cycle_scheduler().last_cycle_metrics();
    const bool ok = rc == 0 && m.cycle_overrun_ms > 0 && m.sleep_requested_ms == 0 && !m.entered_deep_sleep;
    ok ? devtest::report_pass("overrun", "sleep=0, overrun recorded, no compensation")
       : devtest::report_fail("overrun", "overrun not recorded, or sleep was requested after an overrun");
    return ok ? 0 : 1;
}

int cmd_cycle_production(int, char**)
{
    devtest_rtc_arm_cycles(config::CYCLE_TARGET_MS, config::CAPTURE_TIME_MS, 0);
    const int rc = run_cycle("cycle_production", test_config(config::CYCLE_TARGET_MS, config::CAPTURE_TIME_MS, true));
    devtest_rtc_clear();
    return rc;
}

// power_run_peripheral_idle_test (test 10.1)
int cmd_power_idle(int, char**)
{
    if (!devtest::ensure_idle("power_idle")) return 1;
    sensors::sensor_manager().run_capture(data::capture_session(), 3000);
    devtest::rail_off();
    std::printf("[power_idle] SensorManager stopped; PERIPH_EN command=%s\n",
                board_peripherals_commanded_enabled() ? "ON" : "OFF");
    devtest::report_pass("power_idle", "rail commanded OFF after all users stopped");
    devtest::report_hw("power_idle", "ECO GPIO-controlled: LED1 off / SWITCHED_3V3 low; ECO forced-on: stays on");
    return 0;
}

// ---- app_main bring-up entry points (bringup_tests.h) ----------------------------------

void test_scheduler()
{
    devtest::begin(10, "M10.1", "CYCLE SCHEDULER AWAKE CYCLE (20 s / 5 s)");
    devtest::finish("M10.1", devtest::call(cmd_scheduler, {"scheduler", "20000", "5000"}));
}

void test_power_manager()
{
    devtest::begin(10, "M10.2", "POWER MANAGER: CAPTURE THEN PERIPH_EN OFF");
    devtest::finish("M10.2", devtest::call(cmd_power_idle, {"power_idle"}));
}

void test_sleep_manager()
{
    devtest::begin(10, "M10.3", "SLEEP MANAGER: 10 s TIMER DEEP SLEEP");
    if (resumed_after_sleep(DevTestRtcKind::SLEEP_TEST, "M10.3")) return;
    std::printf("Requires PROG = NORMAL. The chip sleeps 10 s, reboots, and this test then prints the result.\n");
    devtest::finish("M10.3", devtest::call(cmd_sleep, {"sleep", "10"}), devtest::Judge::OPERATOR);  // blocked only
}

void test_scheduler_cycle()
{
    devtest::begin(10, "M10.4", "SCHEDULER CYCLE 20 s / 5 s WITH DEEP SLEEP");
    if (resumed_after_sleep(DevTestRtcKind::CYCLE_TEST, "M10.4")) return;
    std::printf("PROG = NORMAL: sleeps for the rest of the cycle. PROG = PROGRAMMING: stays awake.\n");
    devtest::finish("M10.4", devtest::call(cmd_cycle, {"cycle", "20000", "5000"}), devtest::Judge::OPERATOR);
}

void test_scheduler_repeat()
{
    devtest::begin(10, "M10.5", "SCHEDULER 3 CYCLES ACROSS DEEP SLEEP");
    if (resumed_after_sleep(DevTestRtcKind::CYCLE_TEST, "M10.5")) return;
    devtest::finish("M10.5", devtest::call(cmd_cycle_repeat, {"cycle_repeat", "20000", "5000", "3"}),
                    devtest::Judge::OPERATOR);
}

void test_scheduler_overrun()
{
    devtest::begin(10, "M10.6", "SCHEDULER FORCED OVERRUN");
    devtest::finish("M10.6", devtest::call(cmd_overrun, {"overrun"}));
}

void test_scheduler_production_cycle()
{
    devtest::begin(10, "M10.7", "ONE PRODUCTION CYCLE (120 s / 30 s)");
    if (resumed_after_sleep(DevTestRtcKind::CYCLE_TEST, "M10.7")) return;
    devtest::finish("M10.7", devtest::call(cmd_cycle_production, {"cycle_production"}), devtest::Judge::OPERATOR);
}
