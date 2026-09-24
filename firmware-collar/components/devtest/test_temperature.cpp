// Material 6: temperature tests (real one-shot backend + real MCP9700/MF58 drivers).
#include <cmath>
#include <cstdio>
#include "analog_adc_engine.h"
#include "bringup_tests.h"
#include "cownect_err.h"
#include "devtest_common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sensor_manager.h"
#include "temperature_math.h"

using namespace cownect;

namespace {

enum Which { BOARD = 1, COW = 2, BOTH = 3 };

int run_temperature(const char* test, uint32_t seconds, int which)
{
    if (!devtest::ensure_idle(test)) return 1;
    devtest::rail_on();
    sensors::SensorSet& set = sensors::sensor_set();
    esp_err_t err = set.oneshot_adc.init();
    if (err != ESP_OK) {
        char why[80];
        std::snprintf(why, sizeof(why), "one-shot ADC1 init failed (%s)", cownect_err_name(err));
        devtest::report_fail(test, why);
        return 1;
    }
    set.board_temperature.set_adc(set.oneshot_adc);
    set.cow_temperature.set_adc(set.oneshot_adc);
    sensors::TemperatureCaptureStats st = {};
    for (uint32_t t = 0; t < seconds; ++t) {  // controlled 1 Hz logical rate
        if (which & BOARD) {
            sensors::BoardTemperatureSample b = {};
            b.timestamp_ms = t * 1000;
            set.board_temperature.sample(b);
            sensors::temperature_account_board(st, b);
            std::printf("[TEMP] t=%2us board raw=%d mv=%d%s temp=%.2fC status=%s\n", static_cast<unsigned>(t),
                        static_cast<int>(b.adc_raw), static_cast<int>(b.millivolts), b.millivolts_valid ? "" : "(invalid)", b.temperature_c,
                        sensors::temperature_status_name(b.status));
        }
        if (which & COW) {
            sensors::CowTemperatureSample c = {};
            c.timestamp_ms = t * 1000;
            set.cow_temperature.sample(c);
            sensors::temperature_account_cow(st, c);
            std::printf("[TEMP] t=%2us cow   raw=%d mv=%d%s R=%.0fohm temp=%.2fC status=%s\n", static_cast<unsigned>(t),
                        static_cast<int>(c.adc_raw), static_cast<int>(c.millivolts), c.millivolts_valid ? "" : "(invalid)", c.resistance_ohm, c.temperature_c,
                        sensors::temperature_status_name(c.status));
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    set.oneshot_adc.deinit();
    set.board_temperature.set_adc(set.preset_adc);
    set.cow_temperature.set_adc(set.preset_adc);
    std::printf("[TEMP] board attempted=%u valid=%u adc_err=%u conv_err=%u | cow attempted=%u valid=%u adc_err=%u "
                "conv_err=%u open=%u short=%u\n",
                static_cast<unsigned>(st.board_samples_attempted), static_cast<unsigned>(st.board_samples_valid),
                static_cast<unsigned>(st.board_adc_errors), static_cast<unsigned>(st.board_conversion_errors),
                static_cast<unsigned>(st.cow_samples_attempted), static_cast<unsigned>(st.cow_samples_valid),
                static_cast<unsigned>(st.cow_adc_errors), static_cast<unsigned>(st.cow_conversion_errors),
                static_cast<unsigned>(st.cow_open_suspected), static_cast<unsigned>(st.cow_short_suspected));
    const bool adc_ok = st.board_adc_errors == 0 && st.cow_adc_errors == 0;
    adc_ok ? devtest::report_pass(test, "ADC reads without driver errors")
           : devtest::report_fail(test, "ADC read errors");
    devtest::report_hw(test,
                       "room temperature plausible vs reference thermometer; warm/cool direction correct; "
                       "probe unplugged -> OPEN_SUSPECTED/invalid, shorted -> SHORT_SUSPECTED/invalid "
                       "(thresholds NEEDS_HARDWARE_VALIDATION)");
    return adc_ok ? 0 : 1;
}

}  // namespace

int cmd_temperature(int argc, char** argv)
{
    return run_temperature("temperature", devtest::arg_u32(argc, argv, 1, 30), BOTH);
}

int cmd_temp_board(int argc, char** argv)
{
    return run_temperature("temp_board", devtest::arg_u32(argc, argv, 1, 10), BOARD);
}

int cmd_temp_cow(int argc, char** argv)
{
    return run_temperature("temp_cow", devtest::arg_u32(argc, argv, 1, 10), COW);
}

// Material 8 section 36: one-shot must be refused while ADC1 continuous is owned.
int cmd_adc_conflict(int, char**)
{
    if (!devtest::ensure_idle("adc_conflict")) return 1;
    devtest::rail_on();
    sensors::SensorSet& set = sensors::sensor_set();
    set.microphone.set_integrated_mode(false);
    if (set.microphone.init() != ESP_OK || set.microphone.start_capture() != ESP_OK) {
        devtest::report_fail("adc_conflict", "could not start microphone continuous mode");
        return 1;
    }
    const esp_err_t err = set.oneshot_adc.init();
    vTaskDelay(pdMS_TO_TICKS(200));
    set.microphone.stop_capture();
    if (err == COWNECT_ERR_RESOURCE_BUSY) {
        devtest::report_pass("adc_conflict", "one-shot refused while continuous owns ADC1; stream unaffected");
        return 0;
    }
    if (err == ESP_OK) set.oneshot_adc.deinit();
    char why[96];
    std::snprintf(why, sizeof(why), "one-shot ADC1 was not refused while continuous mode owned it (%s)",
                  cownect_err_name(err));
    devtest::report_fail("adc_conflict", why);
    return 1;
}

// ---- app_main bring-up entry points (bringup_tests.h) ----------------------------------

void test_temperature()
{
    devtest::begin(6, "M6.1", "TEMPERATURE (MCP9700 BOARD + MF58 COW)");
    devtest::finish("M6.1", devtest::call(cmd_temperature, {"temperature", "30"}));
}

void test_mcp9700()
{
    devtest::begin(6, "M6.2", "MCP9700 BOARD TEMPERATURE");
    devtest::finish("M6.2", devtest::call(cmd_temp_board, {"temp_board", "10"}));
}

void test_thermistor()
{
    devtest::begin(6, "M6.3", "MF58 COW THERMISTOR");
    std::printf("Fault checks: unplug the probe (OPEN_SUSPECTED) and short it (SHORT_SUSPECTED) during the run.\n");
    devtest::finish("M6.3", devtest::call(cmd_temp_cow, {"temp_cow", "10"}));
}

void test_adc_conflict()
{
    devtest::begin(6, "M6.4", "ADC1 OWNERSHIP CONFLICT");
    devtest::finish("M6.4", devtest::call(cmd_adc_conflict, {"adc_conflict"}));
}

void test_temperature_math()
{
    devtest::begin(6, "M6.5", "TEMPERATURE CONVERSION MATH (NO HARDWARE)");
    const bool ok = unit_run_group("temperature_math");
    if (!ok) devtest::report_fail("temperature_math", "conversion unit checks failed (FAIL lines above)");
    devtest::finish("M6.5", ok ? 0 : 1);
}
