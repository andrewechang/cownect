#include "cownect_app.h"

#include <cstdio>
#include "board_identity.h"
#include "board_mode.h"
#include "board_power.h"
#include "capture_session.h"
#include "cownect_config.h"
#include "cycle_scheduler.h"
#include "devtest.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rtc_state.h"
#include "sleep_manager.h"

using namespace cownect;

namespace {
constexpr const char* TAG = "COWNECT";

OperationMode wait_for_stable_mode()
{
    OperationMode mode = board_get_operation_mode();
    while (mode == OperationMode::UNSTABLE) {  // bounded per iteration, yields, never sleeps
        vTaskDelay(pdMS_TO_TICKS(200));
        mode = board_get_operation_mode();
    }
    return mode;
}

}  // namespace

void cownect_system_init()
{
    static bool done = false;
    if (done) {
        return;
    }
    done = true;
    board_mode_init();
    board_power_init();  // PERIPH_EN commanded OFF until a cycle/test needs the rail
    data::rtc_state_init_on_boot();
    if (data::capture_storage_init() != ESP_OK) {
        ESP_LOGE(TAG, "microphone capture buffer (PSRAM) unavailable");
    }
}

void run_cownect_firmware()
{
    // 1. Initialization
    cownect_system_init();
    std::printf("\n==== CowNect firmware %s ====\n", board_firmware_version());
    std::printf("reset=%s wake=%s cycle_seq=%u next_capture_id=%u\n", power::reset_reason_name(esp_reset_reason()),
                power::wake_cause_name(esp_sleep_get_wakeup_cause()),
                static_cast<unsigned>(data::rtc_state().cycle_sequence),
                static_cast<unsigned>(data::rtc_state().next_capture_id));
    devtest_print_config_status();

    // A development deep-sleep test started from the console finishes before anything else.
    if (devtest_resume_after_wake()) {
        devtest_start_console();
        return;
    }

    // 2. Determine operation mode
    OperationMode mode = wait_for_stable_mode();
    ESP_LOGI(TAG, "operation mode: %s", operation_mode_name(mode));
    if (mode == OperationMode::PROGRAMMING) {
        devtest_start_console();  // waits for explicit commands; scheduler NOT started
        return;
    }

    // 3-7. NORMAL: every run_one_cycle() powers + initializes the sensors, captures, processes,
    // communicates per the menuconfig communication mode, powers down, then enters timer deep
    // sleep for the rest of the 120 s cycle. The next cycle starts from the wake/boot path.
    scheduler::CycleScheduler& sched = scheduler::cycle_scheduler();
    ESP_ERROR_CHECK(sched.init(scheduler::scheduler_production_config()));
    while (true) {
        sched.run_one_cycle();
        // Returned: overrun, or mode changed near the sleep decision. Re-check the switch.
        mode = wait_for_stable_mode();
        if (mode == OperationMode::PROGRAMMING) {
            ESP_LOGI(TAG, "PROGRAMMING selected - scheduler stopped, console started");
            devtest_start_console();
            return;
        }
    }
}
