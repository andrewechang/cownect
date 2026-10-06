#include "cownect_app.h"

#include <cstdio>
#include "board_identity.h"
#include "board_mode.h"
#include "board_power.h"
#include "capture_session.h"
#include "cownect_config.h"
#include "cycle_scheduler.h"
#include "devtest.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rtc_state.h"
#include "sdkconfig.h"
#include "sleep_manager.h"

// The ESP32-S3-WROOM-1-N16R8 has 8 MB OCTAL PSRAM. A quad-mode image aborts at boot
// ("quad_psram: PSRAM chip is not connected") before app_main, so refuse to build one.
#if !CONFIG_SPIRAM || !CONFIG_SPIRAM_MODE_OCT
#error "N16R8 needs CONFIG_SPIRAM=y and CONFIG_SPIRAM_MODE_OCT=y (see sdkconfig.defaults); delete sdkconfig and rebuild"
#endif

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
        ESP_LOGE(TAG, "microphone capture buffer unavailable (PSRAM and internal fallback failed)");
    }
    // One line per boot so every test log shows the memory situation.
    const bool psram = esp_psram_is_initialized();
    std::printf("[BOOT] reset=%s PSRAM=%s size=%u free=%u | internal free=%u largest=%u | mic buffer=%s %u samples\n",
                power::reset_reason_name(esp_reset_reason()), psram ? "OK" : "NOT DETECTED",
                static_cast<unsigned>(psram ? esp_psram_get_size() : 0),
                static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
                static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
                !data::capture_storage_microphone_available() ? "NONE"
                : data::capture_storage_microphone_in_psram() ? "PSRAM"
                                                               : "INTERNAL(fallback)",
                static_cast<unsigned>(data::capture_storage().mic_capacity));
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
