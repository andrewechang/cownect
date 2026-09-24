// Material 7: GPS tests (real ATGM336H driver; no proprietary commands are ever sent).
#include <cstdio>
#include "bringup_tests.h"
#include "devtest_common.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sensor_manager.h"

using namespace cownect;

namespace {

struct PrintBudget {
    uint32_t remaining;
};

void print_sentence(const char* body, void* ctx)
{
    auto* b = static_cast<PrintBudget*>(ctx);
    if (b->remaining > 0) {
        b->remaining--;
        std::printf("  $%s\n", body);
    }
}

bool gps_prepare(const char* test)
{
    if (!devtest::ensure_idle(test)) return false;
    if (devtest::rail_on() != ESP_OK || sensors::sensor_set().gps.init() != ESP_OK) {
        devtest::report_fail(test, "UART init failed");
        return false;
    }
    return true;
}

void pump(uint32_t duration_ms)
{
    auto& gps = sensors::sensor_set().gps;
    const int64_t end = esp_timer_get_time() + static_cast<int64_t>(duration_ms) * 1000;
    while (esp_timer_get_time() < end) {
        gps.service();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void print_stats()
{
    const auto& s = sensors::sensor_set().gps.stats();
    std::printf("[GPS] bytes=%u sentences=%u checksum_err=%u parse_err=%u unsupported=%u overlength=%u uart_ovf=%u "
                "valid_fix=%u invalid=%u dropped=%u dup=%u\n",
                static_cast<unsigned>(s.uart_bytes_received), static_cast<unsigned>(s.framed_sentence_count),
                static_cast<unsigned>(s.checksum_error_count), static_cast<unsigned>(s.parse_error_count),
                static_cast<unsigned>(s.unsupported_sentence_count), static_cast<unsigned>(s.overlength_count),
                static_cast<unsigned>(s.uart_overflow_count), static_cast<unsigned>(s.valid_fix_count),
                static_cast<unsigned>(s.invalid_fix_count), static_cast<unsigned>(s.dropped_fix_count),
                static_cast<unsigned>(s.duplicate_epoch_count));
}

}  // namespace

// gps_run_uart_test (Material 4 stage 4.7 / Material 7 test 7.1)
int cmd_gps(int argc, char** argv)
{
    if (!gps_prepare("gps")) return 1;
    auto& gps = sensors::sensor_set().gps;
    gps.reset_statistics();
    PrintBudget budget = {8};  // bounded NMEA printing during bring-up only
    gps.set_sentence_observer(&print_sentence, &budget);
    pump(devtest::arg_u32(argc, argv, 1, 10) * 1000);
    gps.set_sentence_observer(nullptr, nullptr);
    print_stats();
    const auto& s = gps.stats();
    const bool ok = s.uart_bytes_received > 0 && s.framed_sentence_count > 0;
    ok ? devtest::report_pass("gps", "NMEA framed at 9600 8N1 (no fix required)")
       : devtest::report_fail("gps", "no NMEA traffic - check rail, UART direction, module");
    devtest::report_hw("gps", "RX on GPIO17 / TX on GPIO18 confirmed by traffic; fix requires sky view");
    return ok ? 0 : 1;
}

// gps_run_sentence_discovery_test (Material 7 test 7.2)
int cmd_gps_discovery(int argc, char** argv)
{
    if (!gps_prepare("gps_discovery")) return 1;
    auto& gps = sensors::sensor_set().gps;
    gps.reset_statistics();
    pump(devtest::arg_u32(argc, argv, 1, 20) * 1000);
    size_t n = 0;
    const sensors::GpsSentenceStat* st = gps.sentence_stats(n);
    std::printf("[GPS] observed sentence identifiers:\n");
    for (size_t i = 0; i < n; ++i) {
        std::printf("  %-8s count=%u checksum_ok=%u\n", st[i].identifier, static_cast<unsigned>(st[i].count),
                    static_cast<unsigned>(st[i].checksum_ok));
    }
    print_stats();
    if (n == 0) {
        devtest::report_fail("gps_discovery", "no NMEA sentence identifiers observed - check rail, UART, module");
        return 1;
    }
    devtest::report_pass("gps_discovery", "sentence identifiers listed above");
    devtest::report_hw("gps_discovery",
                       "record identifiers in hardware_questions.md; parser uses standard RMC/GGA only");
    return 0;
}

// gps_run_capture_test (Material 7 tests 7.3-7.5)
int cmd_gps_capture(int argc, char** argv)
{
    if (!gps_prepare("gps_capture")) return 1;
    auto& gps = sensors::sensor_set().gps;
    gps.start_capture();
    pump(devtest::arg_u32(argc, argv, 1, 30) * 1000);
    gps.stop_capture();
    print_stats();
    for (size_t i = 0; i < gps.fix_count(); ++i) {
        const auto& f = gps.fixes()[i];
        if (i < 3 || i + 1 == gps.fix_count()) {
            std::printf("  fix[%u] t=%ums lat_e7=%ld lon_e7=%ld q=%u sats=%u utc=%02u:%02u:%02u\n",
                        static_cast<unsigned>(i), static_cast<unsigned>(f.capture_offset_ms),
                        static_cast<long>(f.latitude_e7), static_cast<long>(f.longitude_e7), f.fix_quality, f.satellites,
                        f.utc_hour, f.utc_minute, f.utc_second);
        }
    }
    std::printf("[GPS] retained fixes=%u (no-fix is not a failure)\n", static_cast<unsigned>(gps.fix_count()));
    devtest::report_pass("gps_capture", "capture ended on time");
    devtest::report_hw("gps_capture", "outdoor: plausible coordinates ~1 Hz; indoor: zero fixes, firmware responsive");
    return 0;
}

// gps_run_power_cycle_test (Material 7 test 7.6)
int cmd_gps_power(int argc, char** argv)
{
    if (!gps_prepare("gps_power")) return 1;
    const uint32_t max_s = devtest::arg_u32(argc, argv, 1, 60);
    auto& gps = sensors::sensor_set().gps;
    devtest::rail_off();
    std::printf("[GPS] VCC commanded OFF (VBAT stays on ALWAYS_ON_3V3) for 10 s\n");
    vTaskDelay(pdMS_TO_TICKS(10000));
    devtest::rail_on();
    gps.init();
    gps.start_capture();
    const int64_t t0 = esp_timer_get_time();
    int64_t first_byte = -1;
    int64_t first_fix = -1;
    while (esp_timer_get_time() - t0 < static_cast<int64_t>(max_s) * 1000000) {
        gps.service();
        if (first_byte < 0 && gps.stats().uart_bytes_received > 0) first_byte = esp_timer_get_time() - t0;
        if (first_fix < 0 && gps.stats().valid_fix_count > 0) {
            first_fix = esp_timer_get_time() - t0;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    gps.stop_capture();
    std::printf("[GPS] after power restore: first NMEA byte %lld ms, first valid fix %lld ms (-1 = none in %us)\n",
                first_byte < 0 ? -1LL : static_cast<long long>(first_byte / 1000),
                first_fix < 0 ? -1LL : static_cast<long long>(first_fix / 1000), static_cast<unsigned>(max_s));
    first_byte >= 0 ? devtest::report_pass("gps_power", "NMEA resumed without reboot")
                    : devtest::report_fail("gps_power", "no NMEA after power restore");
    devtest::report_hw("gps_power", "compare TTFF with datasheet hot start only under good sky view");
    return first_byte >= 0 ? 0 : 1;
}

// ---- app_main bring-up entry points (bringup_tests.h) ----------------------------------

void test_gps()
{
    devtest::begin(7, "M7.1", "ATGM336H GPS UART / NMEA");
    devtest::finish("M7.1", devtest::call(cmd_gps, {"gps", "10"}));
}

void test_gps_discovery()
{
    devtest::begin(7, "M7.2", "GPS NMEA SENTENCE DISCOVERY");
    devtest::finish("M7.2", devtest::call(cmd_gps_discovery, {"gps_discovery", "20"}));
}

void test_gps_capture()
{
    devtest::begin(7, "M7.3", "GPS FIX CAPTURE (NO FIX IS NOT A FAILURE)");
    devtest::finish("M7.3", devtest::call(cmd_gps_capture, {"gps_capture", "30"}));
}

void test_gps_power_cycle()
{
    devtest::begin(7, "M7.4", "GPS VCC OFF/ON (VBAT RETAINED)");
    devtest::finish("M7.4", devtest::call(cmd_gps_power, {"gps_power", "60"}));
}

void test_nmea_parser()
{
    devtest::begin(7, "M7.5", "NMEA FRAMER / PARSER (NO HARDWARE)");
    const bool ok = unit_run_group("nmea");
    if (!ok) devtest::report_fail("nmea", "NMEA unit checks failed (FAIL lines above)");
    devtest::finish("M7.5", ok ? 0 : 1);
}
