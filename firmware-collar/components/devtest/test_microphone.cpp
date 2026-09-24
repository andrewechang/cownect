// Material 8: microphone tests (real continuous ADC engine, MIC-only pattern at 32 kS/s).
#include <cstdio>
#include "analog_adc_engine.h"
#include "bringup_tests.h"
#include "cownect_config.h"
#include "devtest_common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sensor_manager.h"

using namespace cownect;

namespace {

bool mic_run(const char* test, uint32_t duration_ms)
{
    auto& mic = sensors::sensor_set().microphone;
    mic.set_integrated_mode(false);
    esp_err_t err = mic.init();
    if (err == ESP_OK) err = mic.start_capture();
    if (err != ESP_OK) {
        char why[80];
        std::snprintf(why, sizeof(why), "continuous ADC (GPIO5) init/start failed (%s)", esp_err_to_name(err));
        devtest::report_fail(test, why);
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    mic.stop_capture();
    return true;
}

void print_mic(uint32_t duration_ms)
{
    const auto& s = sensors::sensor_set().microphone.stats();
    const auto& a = sensors::analog_engine().stats();
    std::printf("[MIC] samples=%u nominal=%u elapsed=%.3fs effective_rate=%.1fHz\n", static_cast<unsigned>(s.samples_stored),
                static_cast<unsigned>(config::MIC_SAMPLE_RATE_HZ / 1000 * duration_ms),
                (s.capture_end_us - s.capture_start_us) / 1e6, s.effective_sample_rate_hz);
    std::printf("[MIC] mean=%.1f rms_ac=%.2f min=%u max=%u low_rail=%u high_rail=%u\n", s.raw_mean, s.raw_rms_ac, s.raw_min,
                s.raw_max, static_cast<unsigned>(s.low_rail_count), static_cast<unsigned>(s.high_rail_count));
    std::printf("[MIC] frames=%u pool_ovf=%u dst_ovf=%u timeouts=%u adc_err=%u wrong_channel=%u\n",
                static_cast<unsigned>(s.adc_frames_received), static_cast<unsigned>(s.driver_pool_overflow_count),
                static_cast<unsigned>(s.destination_overflow_count), static_cast<unsigned>(s.read_timeout_count),
                static_cast<unsigned>(s.adc_error_count), static_cast<unsigned>(a.unexpected_channel_results + a.invalid_results));
}

bool mic_clean()
{
    const auto& s = sensors::sensor_set().microphone.stats();
    return s.samples_stored > 0 && s.driver_pool_overflow_count == 0 && s.destination_overflow_count == 0 &&
           s.adc_error_count == 0 && s.wrong_channel_results == 0;
}

// Reason text for a capture that is not clean (checked in the same order as mic_clean()).
const char* mic_problem()
{
    const auto& s = sensors::sensor_set().microphone.stats();
    return s.samples_stored == 0               ? "no microphone samples stored"
           : s.driver_pool_overflow_count      ? "ADC driver pool overflow"
           : s.destination_overflow_count      ? "capture buffer (destination) overflow"
           : s.adc_error_count                 ? "ADC read errors"
           : s.wrong_channel_results           ? "results from an unexpected ADC channel"
                                               : "";
}

}  // namespace

// microphone_run_capture_test (tests 8.2 / 8.3)
int cmd_microphone(int argc, char** argv)
{
    if (!devtest::ensure_idle("microphone")) return 1;
    devtest::rail_on();
    const uint32_t ms = devtest::arg_u32(argc, argv, 1, 30) * 1000;
    if (!mic_run("microphone", ms)) return 1;
    print_mic(ms);
    const bool ok = mic_clean();
    ok ? devtest::report_pass("microphone") : devtest::report_fail("microphone", mic_problem());
    devtest::report_hw("microphone",
                       "quiet room: mean away from rails; sound raises rms_ac; no clipping at normal levels "
                       "(attenuation PROVISIONAL)");
    return ok ? 0 : 1;
}

// microphone_run_adc_stream_test (test 8.1)
int cmd_mic_stream(int argc, char** argv)
{
    if (!devtest::ensure_idle("mic_stream")) return 1;
    devtest::rail_on();
    const uint32_t ms = devtest::arg_u32(argc, argv, 1, 3) * 1000;
    if (!mic_run("mic_stream", ms)) return 1;
    print_mic(ms);
    const auto& a = sensors::analog_engine().stats();
    std::printf("[MIC] observed channels:");
    for (uint8_t i = 0; i < a.observed_count; ++i) std::printf(" %u", a.observed_first_channels[i]);
    std::printf(" (expected all 4 = ADC1_CH4)\n");
    const bool ok = a.frames_received > 0 && mic_clean();
    ok ? devtest::report_pass("mic_stream")
       : devtest::report_fail("mic_stream", a.frames_received == 0 ? "no ADC frames received" : mic_problem());
    return ok ? 0 : 1;
}

// microphone_run_overflow_test (test 8.4) - development fault injection only.
int cmd_mic_overflow(int, char**)
{
    if (!devtest::ensure_idle("mic_overflow")) return 1;
    devtest::rail_on();
    sensors::analog_engine().set_fault_injection_delay_ms(50);
    const bool ran = mic_run("mic_overflow", 2000);
    sensors::analog_engine().set_fault_injection_delay_ms(0);  // never left enabled
    if (!ran) return 1;
    print_mic(2000);
    const bool detected = sensors::sensor_set().microphone.stats().driver_pool_overflow_count > 0;
    // Recovery with a normal capture.
    const bool recovered = mic_run("mic_overflow", 1000) && mic_clean();
    print_mic(1000);
    (detected && recovered) ? devtest::report_pass("mic_overflow", "overflow detected; next capture clean")
                            : devtest::report_fail("mic_overflow", !detected ? "deliberate overflow was not detected"
                                                                             : "capture after the overflow not clean");
    return (detected && recovered) ? 0 : 1;
}

// microphone_run_power_cycle_test (test 8.6)
int cmd_mic_power(int, char**)
{
    if (!devtest::ensure_idle("mic_power")) return 1;
    devtest::rail_on();
    bool ok = mic_run("mic_power", 1000);
    devtest::rail_off();
    vTaskDelay(pdMS_TO_TICKS(1000));
    devtest::rail_on();
    ok = ok && mic_run("mic_power", 1000) && mic_clean();
    print_mic(1000);
    ok ? devtest::report_pass("mic_power", "capture works after rail off/on")
       : devtest::report_fail("mic_power", "capture after rail off/on failed or not clean");
    return ok ? 0 : 1;
}

// Bounded debug preview (Material 8 section 40) - never the whole buffer.
int cmd_mic_dump(int argc, char** argv)
{
    const uint32_t n = devtest::arg_u32(argc, argv, 1, 32);
    const auto& mic = sensors::sensor_set().microphone;
    const size_t count = mic.sample_count();
    if (count == 0 || mic.raw_samples() == nullptr) {
        devtest::report_fail("mic_dump", "no microphone samples retained (run a microphone capture first)");
        return 1;
    }
    std::printf("format=uint16 raw ADC code, rate=%u Hz, count=%u, atten=PROVISIONAL\nfirst:",
                static_cast<unsigned>(config::MIC_SAMPLE_RATE_HZ), static_cast<unsigned>(count));
    for (size_t i = 0; i < n && i < count; ++i) std::printf(" %u", mic.raw_samples()[i]);
    std::printf("\nlast:");
    for (size_t i = count > n ? count - n : 0; i < count; ++i) std::printf(" %u", mic.raw_samples()[i]);
    std::printf("\n");
    devtest::report_hw("mic_dump", "codes vary with sound and stay away from 0 / 4095 in a quiet room");
    return 0;
}

// ---- app_main bring-up entry points (bringup_tests.h) ----------------------------------

void test_microphone()
{
    devtest::begin(8, "M8.1", "MICROPHONE STANDALONE CAPTURE (32 kS/s)");
    devtest::finish("M8.1", devtest::call(cmd_microphone, {"microphone", "30"}));
}

void test_mic_stream()
{
    devtest::begin(8, "M8.2", "MICROPHONE ADC STREAM IDENTIFICATION");
    devtest::finish("M8.2", devtest::call(cmd_mic_stream, {"mic_stream", "3"}));
}

void test_mic_overflow()
{
    devtest::begin(8, "M8.3", "MICROPHONE OVERFLOW (FAULT INJECTION)");
    devtest::finish("M8.3", devtest::call(cmd_mic_overflow, {"mic_overflow"}));
}

void test_mic_power_cycle()
{
    devtest::begin(8, "M8.4", "MICROPHONE RAIL OFF/ON RE-INIT");
    devtest::finish("M8.4", devtest::call(cmd_mic_power, {"mic_power"}));
}

void test_mic_dump()
{
    devtest::begin(8, "M8.5", "MICROPHONE RAW SAMPLE DUMP");
    int rc = 1;
    if (devtest::ensure_idle("mic_dump")) {
        devtest::rail_on();
        if (mic_run("mic_dump", 1000)) {  // fresh 1 s capture so the test runs on its own
            print_mic(1000);
            rc = devtest::call(cmd_mic_dump, {"mic_dump", "32"});
        }
    }
    devtest::finish("M8.5", rc, devtest::Judge::OPERATOR);
}
