// Material 15: hybrid_run_once_test (real capture, feature processor, LoRa, Wi-Fi uploader).
#include <cstdio>
#include "bringup_tests.h"
#include "capture_session.h"
#include "communication_manager.h"
#include "cownect_config.h"
#include "cownect_err.h"
#include "cycle_scheduler.h"
#include "devtest_common.h"
#include "lora_config.h"
#include "prototype_wifi_config.h"

using namespace cownect;

void devtest_print_telemetry(const features::TelemetryRecord& t, uint32_t processing_ms);

namespace {

// Refuses up front (before any capture) when a transport is disabled or CONFIG_NOT_SET.
// The same gates are enforced again inside the radio adapter and the Wi-Fi uploader.
int hybrid_preflight(const char* test)
{
    const hybrid::HybridPrototypeConfig hc = hybrid::hybrid_config_from_menuconfig();
    if (!hc.enable_lora_telemetry || !hc.enable_wifi_raw_upload) {
        devtest::report_blocked(test, COWNECT_ERR_NOT_CONFIGURED,
                                !hc.enable_lora_telemetry ? "COWNECT_HYBRID_ENABLE_LORA_TELEMETRY disabled in menuconfig"
                                                          : "COWNECT_HYBRID_ENABLE_WIFI_UPLOAD disabled in menuconfig");
        return 2;
    }
    const char* reason = nullptr;
    esp_err_t err = radio::lora_tx_gate(&reason);
    if (err != ESP_OK) {
        char why[160];
        std::snprintf(why, sizeof(why), "LoRa TX gate: %s", reason);
        devtest::report_blocked(test, err, why);
        return 2;
    }
    wifi::PrototypeWifiConfig w;
    const char* missing = nullptr;
    err = wifi::prototype_wifi_config_load(w, &missing);
    if (err != ESP_OK) {
        char why[160];
        std::snprintf(why, sizeof(why), "Wi-Fi/Jetson %s", missing ? missing : "configuration");
        devtest::report_blocked(test, err, why);
        return 2;
    }
    std::printf("[HYBRID] gates OK - the LoRa telemetry packet is transmitted right after the capture\n");
    devtest::rf_tx_countdown();
    return 0;
}

int hybrid_once(const char* test, uint32_t capture_ms)
{
    if (!devtest::ensure_idle(test)) return 1;
    const int gate = hybrid_preflight(test);
    if (gate != 0) return gate;
    // One scheduler cycle, forced HYBRID, awake (PROGRAMMING console context).
    scheduler::SchedulerConfig cfg = scheduler::scheduler_production_config();
    cfg.capture_ms = capture_ms;
    cfg.cycle_target_ms = config::CYCLE_TARGET_MS;
    cfg.allow_deep_sleep = false;
    cfg.wait_until_boundary = false;
    cfg.communication_enabled = true;
    cfg.communication_mode = comm::CommunicationMode::HYBRID_LORA_WIFI;
    scheduler::cycle_scheduler().init(cfg);
    scheduler::cycle_scheduler().run_one_cycle();
    const auto& m = scheduler::cycle_scheduler().last_cycle_metrics();
    const auto& h = comm::communication_manager().last_hybrid_stats();
    devtest_print_telemetry(scheduler::cycle_scheduler().last_telemetry(), m.processing_time_ms);
    std::printf("[HYBRID] capture=%u lora attempted=%d ok=%d bytes=%u ms=%u (%s) | wifi attempted=%d ok=%d bytes=%u ms=%u "
                "stage=%s | total=%ums result=%s awake=%ums\n",
                static_cast<unsigned>(h.capture_id), h.lora_attempted, h.lora_success, static_cast<unsigned>(h.lora_bytes),
                static_cast<unsigned>(h.lora_time_ms), esp_err_to_name(h.lora_error), h.wifi_attempted, h.wifi_success,
                static_cast<unsigned>(h.wifi_bytes), static_cast<unsigned>(h.wifi_time_ms),
                h.wifi.failure_stage ? h.wifi.failure_stage : "-",
                static_cast<unsigned>(h.total_communication_time_ms), hybrid::hybrid_result_name(h.result),
                static_cast<unsigned>(m.awake_elapsed_ms));
    const bool ok = h.result == hybrid::HybridResult::BOTH_OK;
    char why[160];
    if (!m.capture_complete) {
        std::snprintf(why, sizeof(why), "capture did not complete - no communication attempted");
    } else {
        std::snprintf(why, sizeof(why), "%s (LoRa: %s, Wi-Fi stage: %s)", hybrid::hybrid_result_name(h.result),
                      h.lora_success ? "OK" : cownect_err_name(h.lora_error),
                      h.wifi_success ? "OK" : (h.wifi.failure_stage ? h.wifi.failure_stage : "-"));
    }
    ok ? devtest::report_pass(test, "LoRa telemetry and Wi-Fi raw upload both completed")
       : devtest::report_fail(test, why);
    devtest::report_hw(test, "same capture_id on Heltec/Jetson telemetry and Jetson raw file");
    return ok ? 0 : 1;
}

}  // namespace

int cmd_hybrid_short(int, char**)
{
    return hybrid_once("hybrid_short", 5000);
}

int cmd_hybrid_full(int, char**)
{
    return hybrid_once("hybrid_full", config::CAPTURE_TIME_MS);
}

int cmd_hybrid_repeat(int argc, char** argv)
{
    const uint32_t count = devtest::arg_u32(argc, argv, 1, 3);
    int rc = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const int r = hybrid_once("hybrid_repeat", 5000);
        if (r == 2) return 2;  // blocked: no point repeating
        rc |= r;
    }
    return rc;
}

// ---- app_main bring-up entry points (bringup_tests.h) ----------------------------------

void test_hybrid_short()
{
    devtest::begin(15, "M15.1", "HYBRID: 5 s CAPTURE -> LORA TELEMETRY -> WI-FI RAW", true);
    devtest::finish("M15.1", devtest::call(cmd_hybrid_short, {"hybrid_short"}));
}

void test_hybrid_full()
{
    devtest::begin(15, "M15.2", "HYBRID: FULL CAPTURE -> LORA TELEMETRY -> WI-FI RAW", true);
    devtest::finish("M15.2", devtest::call(cmd_hybrid_full, {"hybrid_full"}));
}

void test_hybrid_repeat()
{
    devtest::begin(15, "M15.3", "HYBRID: 3 SHORT CYCLES", true);
    devtest::finish("M15.3", devtest::call(cmd_hybrid_repeat, {"hybrid_repeat", "3"}));
}
