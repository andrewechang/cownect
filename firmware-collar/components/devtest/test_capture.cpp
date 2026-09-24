// Material 9: integrated capture tests (real SensorManager + shared ADC engine, radios off).
#include <cstdio>
#include "analog_adc_engine.h"
#include "bringup_tests.h"
#include "capture_session.h"
#include "cownect_config.h"
#include "devtest_common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sensor_manager.h"

using namespace cownect;

namespace {

bool capture_ok(const data::CaptureSession& s)
{
    const auto& d = s.diagnostics;
    return s.state == data::CaptureState::COMPLETE && d.adc.pool_overflow_count == 0 &&
           d.microphone.destination_overflow_count == 0 && d.accelerometer.fifo_overrun_count == 0 &&
           d.adc.driver_error_count == 0 && s.status.adc_shared_ok;
}

int run_capture(const char* test, uint32_t seconds)
{
    if (!devtest::ensure_idle(test)) return 1;
    data::CaptureSession& s = data::capture_session();
    std::printf("[CAP] SensorManager capture %u s (accelerometer, GPS, microphone, temperatures; radios off)\n",
                static_cast<unsigned>(seconds));
    esp_err_t err = sensors::sensor_manager().run_capture(s, seconds * 1000);
    if (err != ESP_OK) {
        char why[80];
        std::snprintf(why, sizeof(why), "SensorManager::run_capture failed (%s)", esp_err_to_name(err));
        devtest::report_fail(test, why);
        return 1;
    }
    std::printf("[CAP] id=%u nominal: mic=%u accel=%u temps=%u each\n", static_cast<unsigned>(s.id.capture_id),
                static_cast<unsigned>(config::MIC_SAMPLE_RATE_HZ * seconds),
                static_cast<unsigned>(config::ACCEL_ODR_HZ * seconds), static_cast<unsigned>(seconds));
    std::printf("[CAP] status accel=%d(deg %d) gps_uart=%d fix=%d mic=%d(deg %d) tboard=%d tcow=%d adc=%d probe_fault=%d\n",
                s.status.accel_ok, s.status.accel_degraded, s.status.gps_ok, s.status.gps_had_valid_fix,
                s.status.microphone_ok, s.status.microphone_degraded, s.status.board_temp_ok, s.status.cow_temp_ok,
                s.status.adc_shared_ok, s.status.cow_probe_fault);
    const bool ok = capture_ok(s);
    const auto& d = s.diagnostics;
    const char* why = s.state != data::CaptureState::COMPLETE          ? "capture did not reach COMPLETE"
                      : d.adc.pool_overflow_count                     ? "ADC driver pool overflow"
                      : d.microphone.destination_overflow_count       ? "microphone buffer overflow"
                      : d.accelerometer.fifo_overrun_count            ? "accelerometer FIFO overrun"
                      : d.adc.driver_error_count                      ? "ADC driver errors"
                                                                      : "shared ADC pattern not OK (adc_shared_ok=0)";
    ok ? devtest::report_pass(test, "no ADC/pool/destination/FIFO overflow") : devtest::report_fail(test, why);
    devtest::report_hw(test, "no watchdog reset; counts near nominal; common attenuation + cross-channel "
                             "contamination still to be characterized");
    return ok ? 0 : 1;
}

}  // namespace

// sensor_manager_run_integrated_test / sensor_manager_run_full_capture_test
int cmd_capture(int argc, char** argv)
{
    return run_capture("capture", devtest::arg_u32(argc, argv, 1, 5));
}

// analog_run_integrated_pattern_test (Material 9 section 26)
int cmd_adc_pattern(int argc, char** argv)
{
    if (!devtest::ensure_idle("adc_pattern")) return 1;
    devtest::rail_on();
    const uint32_t seconds = devtest::arg_u32(argc, argv, 1, 5);
    const data::CaptureStorage& st = data::capture_storage();
    sensors::AnalogEngineConfig cfg = {};
    cfg.pattern = sensors::kIntegratedPattern;
    cfg.pattern_len = 4;
    cfg.sample_freq_hz = config::INTEGRATED_ADC_TOTAL_HZ;
    cfg.mic = {st.mic, st.mic_capacity};
    cfg.temperature_buckets = true;
    sensors::AnalogAdcEngine& eng = sensors::analog_engine();
    esp_err_t err = eng.start(cfg);
    if (err != ESP_OK) {
        char why[96];
        std::snprintf(why, sizeof(why), "AnalogAdcEngine start with 4-slot pattern refused (%s)", esp_err_to_name(err));
        devtest::report_fail("adc_pattern", why);
        return 1;
    }
    vTaskDelay(pdMS_TO_TICKS(seconds * 1000));
    eng.stop();
    sensors::AnalogBucketResult b;
    uint32_t buckets = 0;
    while (eng.pop_bucket(b)) buckets++;
    const auto& a = eng.stats();
    const double el = (eng.stop_us() - eng.start_us()) / 1e6;
    std::printf("[ADC] total=%llu mic=%llu tboard=%llu tcow=%llu elapsed=%.3fs mic_rate=%.1fHz buckets=%u\n",
                static_cast<unsigned long long>(a.total_results), static_cast<unsigned long long>(a.microphone_results),
                static_cast<unsigned long long>(a.board_temp_results), static_cast<unsigned long long>(a.cow_temp_results),
                el, a.microphone_results / el, static_cast<unsigned>(buckets));
    std::printf("[ADC] pattern_err=%u unexpected=%u invalid=%u pool_ovf=%u timeouts=%u driver_err=%u order:",
                static_cast<unsigned>(a.pattern_order_errors), static_cast<unsigned>(a.unexpected_channel_results),
                static_cast<unsigned>(a.invalid_results), static_cast<unsigned>(a.pool_overflow_count),
                static_cast<unsigned>(a.read_timeout_count), static_cast<unsigned>(a.driver_error_count));
    for (uint8_t i = 0; i < a.observed_count; ++i) std::printf(" %u", a.observed_first_channels[i]);
    std::printf(" (expect repeating 4,0,4,1)\n");
    const bool ok = a.board_temp_results > 0 && a.cow_temp_results > 0 && a.microphone_results > 0 &&
                    a.pattern_order_errors == 0 && a.pool_overflow_count == 0 && a.driver_error_count == 0;
    const char* why = a.microphone_results == 0 || a.board_temp_results == 0 || a.cow_temp_results == 0
                          ? "a channel of the MIC,TB,MIC,TC pattern produced no results"
                      : a.pattern_order_errors ? "pattern order errors"
                      : a.pool_overflow_count  ? "ADC driver pool overflow"
                                               : "ADC driver errors";
    ok ? devtest::report_pass("adc_pattern") : devtest::report_fail("adc_pattern", why);
    devtest::report_hw("adc_pattern", "cross-channel contamination and common attenuation still to be characterized");
    return ok ? 0 : 1;
}

int cmd_capture_repeat(int argc, char** argv)
{
    const uint32_t count = devtest::arg_u32(argc, argv, 1, 3);
    const uint32_t seconds = devtest::arg_u32(argc, argv, 2, 5);
    int rc = 0;
    for (uint32_t i = 0; i < count; ++i) {
        std::printf("---- capture %u/%u ----\n", static_cast<unsigned>(i + 1), static_cast<unsigned>(count));
        rc |= run_capture("capture_repeat", seconds);
    }
    return rc;
}

int cmd_capture_power(int, char**)
{
    int rc = run_capture("capture_power", 5);
    devtest::rail_off();
    vTaskDelay(pdMS_TO_TICKS(1000));
    rc |= run_capture("capture_power", 5);
    return rc;
}

// ---- app_main bring-up entry points (bringup_tests.h) ----------------------------------

void test_analog_adc_engine()
{
    devtest::begin(9, "M9.1", "ANALOG ADC ENGINE (MIC,TB,MIC,TC PATTERN)");
    devtest::finish("M9.1", devtest::call(cmd_adc_pattern, {"adc_pattern", "5"}));
}

void test_sensor_manager()
{
    devtest::begin(9, "M9.2", "SENSOR MANAGER SHORT INTEGRATED CAPTURE (5 s)");
    devtest::finish("M9.2", devtest::call(cmd_capture, {"capture", "5"}));
}

void test_capture()
{
    char seconds[8];
    std::snprintf(seconds, sizeof(seconds), "%u", static_cast<unsigned>(config::CAPTURE_TIME_MS / 1000));
    devtest::begin(9, "M9.3", "FULL INTEGRATED CAPTURE (PRODUCTION LENGTH)");
    devtest::finish("M9.3", devtest::call(cmd_capture, {"capture", seconds}));
}

void test_capture_repeat()
{
    devtest::begin(9, "M9.4", "REPEATED INTEGRATED CAPTURES");
    devtest::finish("M9.4", devtest::call(cmd_capture_repeat, {"capture_repeat", "3", "5"}));
}

void test_capture_power_cycle()
{
    devtest::begin(9, "M9.5", "CAPTURE, RAIL OFF/ON, CAPTURE");
    devtest::finish("M9.5", devtest::call(cmd_capture_power, {"capture_power"}));
}
