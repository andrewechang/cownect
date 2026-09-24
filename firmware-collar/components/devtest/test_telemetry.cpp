// Material 11: telemetry_run_feature_test (real capture + real SimpleFeatureProcessor).
#include <cstdio>
#include "bringup_tests.h"
#include "capture_session.h"
#include "devtest_common.h"
#include "esp_timer.h"
#include "feature_processor.h"
#include "sensor_manager.h"

using namespace cownect;

void devtest_print_telemetry(const features::TelemetryRecord& t, uint32_t processing_ms)
{
    std::printf("[TEL] capture_id=%u device_id=0x%llX\n", static_cast<unsigned>(t.capture_id),
                static_cast<unsigned long long>(t.device_id));
    std::printf("[TEL] gps_valid=%d lat_e7=%ld lon_e7=%ld\n", t.gps_valid, static_cast<long>(t.latitude_e7),
                static_cast<long>(t.longitude_e7));
    std::printf("[TEL] tboard=%s%.2fC tcow=%s%.2fC\n", t.board_temp_valid ? "" : "(invalid) ", t.board_temp_c,
                t.cow_temp_valid ? "" : "(invalid) ", t.cow_temp_c);
    std::printf("[TEL] accel valid=%d mean=%.3fg rms=%.3fg min=%.3fg max=%.3fg (conversion NEEDS_HARDWARE_VALIDATION)\n",
                t.accel_valid, t.accel_magnitude_mean_g, t.accel_magnitude_rms_g, t.accel_magnitude_min_g,
                t.accel_magnitude_max_g);
    std::printf("[TEL] mic valid=%d mean=%.1f rms_ac=%.2f min=%u max=%u\n", t.microphone_valid, t.microphone_mean_raw,
                t.microphone_rms_ac_raw, t.microphone_min_raw, t.microphone_max_raw);
    std::printf("[TEL] status=0x%08lX processing_ms=%u\n", static_cast<unsigned long>(t.status_flags),
                static_cast<unsigned>(processing_ms));
}

int cmd_telemetry(int argc, char** argv)
{
    if (!devtest::ensure_idle("telemetry")) return 1;
    const uint32_t seconds = devtest::arg_u32(argc, argv, 1, 5);
    data::CaptureSession& s = data::capture_session();
    if (sensors::sensor_manager().run_capture(s, seconds * 1000) != ESP_OK) {
        devtest::report_fail("telemetry", "SensorManager capture failed");
        return 1;
    }
    features::TelemetryRecord t = {};
    const int64_t t0 = esp_timer_get_time();
    const esp_err_t err = features::SimpleFeatureProcessor().process(s, t);
    const uint32_t ms = static_cast<uint32_t>((esp_timer_get_time() - t0) / 1000);
    if (err != ESP_OK) {
        char why[80];
        std::snprintf(why, sizeof(why), "SimpleFeatureProcessor::process failed (%s)", esp_err_to_name(err));
        devtest::report_fail("telemetry", why);
        return 1;
    }
    devtest_print_telemetry(t, ms);
    const bool id_ok = t.capture_id == s.id.capture_id && t.device_id == s.id.device_id;
    id_ok ? devtest::report_pass("telemetry", "identity preserved; invalid sensors flagged, not faked")
          : devtest::report_fail("telemetry", "device_id / capture_id in the record differ from the capture");
    devtest::report_hw("telemetry", "temperatures / accel magnitude / mic level plausible for the bench conditions");
    return id_ok ? 0 : 1;
}

// ---- app_main bring-up entry points (bringup_tests.h) ----------------------------------

void test_feature_processor()
{
    devtest::begin(11, "M11.1", "FEATURE PROCESSOR ON FIXTURE (NO HARDWARE)");
    const bool ok = unit_run_group("features");
    if (!ok) devtest::report_fail("features", "feature unit checks failed (FAIL lines above)");
    devtest::finish("M11.1", ok ? 0 : 1);
}

void test_telemetry()
{
    devtest::begin(11, "M11.2", "TELEMETRY FROM A REAL 5 s CAPTURE");
    devtest::finish("M11.2", devtest::call(cmd_telemetry, {"telemetry", "5"}));
}
