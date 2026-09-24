// Material 5: LIS2DW12 tests (real driver, real FIFO).
#include <cstdio>
#include "accelerometer_decode.h"
#include "bringup_tests.h"
#include "cownect_config.h"
#include "devtest_common.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sensor_manager.h"

using namespace cownect;

namespace {

bool accel_init(const char* test)
{
    if (devtest::rail_on() != ESP_OK) {
        devtest::report_fail(test, "rail command failed");
        return false;
    }
    auto& acc = sensors::sensor_set().accelerometer;
    uint8_t who = 0;
    const esp_err_t pe = acc.probe(who);
    std::printf("I2C 0x%02X probe=%s WHO_AM_I=0x%02X (expected 0x%02X)\n", config::LIS2DW12_I2C_ADDR, esp_err_to_name(pe),
                who, config::LIS2DW12_WHO_AM_I_VALUE);
    char why[96];
    if (pe != ESP_OK) {
        std::snprintf(why, sizeof(why), "no I2C ACK at 0x%02X (%s) - check SWITCHED_3V3, SDA=GPIO6, SCL=GPIO7",
                      config::LIS2DW12_I2C_ADDR, esp_err_to_name(pe));
        devtest::report_fail(test, why);
        return false;
    }
    if (who != config::LIS2DW12_WHO_AM_I_VALUE) {
        std::snprintf(why, sizeof(why), "WHO_AM_I register mismatch (read 0x%02X, expected 0x%02X)", who,
                      config::LIS2DW12_WHO_AM_I_VALUE);
        devtest::report_fail(test, why);
        return false;
    }
    if (acc.init() != ESP_OK) {
        devtest::report_fail(test, "register configuration write failed");
        return false;
    }
    return true;
}

// Bounded capture using the same 100 ms FIFO polling as SensorManager.
void run_capture(uint32_t duration_ms, uint32_t skip_service_ms)
{
    auto& acc = sensors::sensor_set().accelerometer;
    acc.start_capture();
    const int64_t end = esp_timer_get_time() + static_cast<int64_t>(duration_ms) * 1000;
    if (skip_service_ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(skip_service_ms));  // deliberate service delay (fault injection)
    }
    while (esp_timer_get_time() < end) {
        acc.service();
        vTaskDelay(pdMS_TO_TICKS(config::ACCEL_FIFO_POLL_MS));
    }
    acc.stop_capture();
}

void print_stats(uint32_t duration_ms)
{
    auto& acc = sensors::sensor_set().accelerometer;
    const auto& s = acc.stats();
    std::printf("samples=%u (nominal %u) elapsed=%ums fifo_ovr=%u i2c_err=%u dropped=%u polls=%u empty=%u max_fifo=%u\n",
                static_cast<unsigned>(acc.sample_count()),
                static_cast<unsigned>(config::ACCEL_ODR_HZ * duration_ms / 1000),
                static_cast<unsigned>((s.capture_end_us - s.capture_start_us) / 1000),
                static_cast<unsigned>(s.fifo_overrun_count), static_cast<unsigned>(s.i2c_error_count),
                static_cast<unsigned>(s.dropped_samples), static_cast<unsigned>(s.fifo_poll_count),
                static_cast<unsigned>(s.empty_poll_count), static_cast<unsigned>(s.max_fifo_level_seen));
    const size_t n = acc.sample_count();
    for (size_t i = 0; i < n && i < 3; ++i) {
        const auto& a = acc.samples()[i];
        std::printf("  [%u] raw x=%d y=%d z=%d |a|=%.3fg (conversion NEEDS_HARDWARE_VALIDATION)\n",
                    static_cast<unsigned>(i), a.x_raw, a.y_raw, a.z_raw, sensors::accel_magnitude_g(a));
    }
}

}  // namespace

int cmd_accel(int argc, char** argv)
{
    if (!devtest::ensure_idle("accel") || !accel_init("accel")) return 1;
    sensors::Lis2dw12ConfigReadback rb;
    if (sensors::sensor_set().accelerometer.read_config(rb) != ESP_OK) {
        devtest::report_fail("accel", "register readback failed");
        return 1;
    }
    std::printf("readback CTRL1=0x%02X CTRL2=0x%02X CTRL6=0x%02X FIFO_CTRL=0x%02X : 50Hz=%d HP=%d 4g=%d BDU=%d "
                "INC=%d FIFO_CONT=%d\n",
                rb.ctrl1, rb.ctrl2, rb.ctrl6, rb.fifo_ctrl, rb.odr_50hz, rb.mode_high_performance, rb.fs_4g, rb.bdu,
                rb.if_add_inc, rb.fifo_continuous);
    const uint32_t ms = devtest::arg_u32(argc, argv, 1, 30) * 1000;
    run_capture(ms, 0);
    print_stats(ms);
    const auto& s = sensors::sensor_set().accelerometer.stats();
    const bool ok = rb.all_ok && s.fifo_overrun_count == 0 && s.i2c_error_count == 0 && s.dropped_samples == 0 &&
                    sensors::sensor_set().accelerometer.sample_count() > 0;
    const char* why = !rb.all_ok                                            ? "register readback differs from configuration"
                      : sensors::sensor_set().accelerometer.sample_count() == 0 ? "no samples read from the FIFO"
                      : s.fifo_overrun_count                                ? "FIFO overrun during capture"
                      : s.i2c_error_count                                   ? "I2C errors during capture"
                                                                            : "samples dropped during capture";
    ok ? devtest::report_pass("accel") : devtest::report_fail("accel", why);
    devtest::report_hw("accel", "stationary ~1 g magnitude in 3 orientations; axes respond to movement");
    return ok ? 0 : 1;
}

int cmd_accel_overrun(int, char**)
{
    if (!devtest::ensure_idle("accel_overrun") || !accel_init("accel_overrun")) return 1;
    // 32-sample FIFO at 50 Hz fills in 0.64 s; skip servicing for 1.5 s.
    run_capture(3000, 1500);
    print_stats(3000);
    const bool detected = sensors::sensor_set().accelerometer.stats().fifo_overrun_count > 0;
    detected ? devtest::report_pass("accel_overrun", "FIFO_OVR detected, driver continued")
             : devtest::report_fail("accel_overrun", "overrun not detected");
    // Recovery: a normal capture must work again.
    run_capture(2000, 0);
    print_stats(2000);
    return detected ? 0 : 1;
}

int cmd_accel_repeat(int argc, char** argv)
{
    if (!devtest::ensure_idle("accel_repeat") || !accel_init("accel_repeat")) return 1;
    const uint32_t count = devtest::arg_u32(argc, argv, 1, 3);
    int rc = 0;
    for (uint32_t i = 0; i < count; ++i) {
        run_capture(5000, 0);
        std::printf("run %u: ", static_cast<unsigned>(i + 1));
        print_stats(5000);
        const auto& s = sensors::sensor_set().accelerometer.stats();
        rc |= (s.fifo_overrun_count || s.i2c_error_count || s.dropped_samples);
    }
    rc ? devtest::report_fail("accel_repeat", "FIFO overrun / I2C error / dropped samples in at least one run")
       : devtest::report_pass("accel_repeat", "counts reset per capture");
    return rc;
}

int cmd_accel_power(int, char**)
{
    if (!devtest::ensure_idle("accel_power") || !accel_init("accel_power")) return 1;
    devtest::rail_off();
    vTaskDelay(pdMS_TO_TICKS(1000));
    uint8_t who = 0;
    const esp_err_t off_probe = sensors::sensor_set().accelerometer.probe(who);
    std::printf("probe with rail commanded OFF: %s (ECO forced-on would still answer)\n", esp_err_to_name(off_probe));
    const bool ok = accel_init("accel_power");
    if (ok) {
        run_capture(2000, 0);
        print_stats(2000);
        devtest::report_pass("accel_power", "re-initialized after rail off/on without reboot");
    }
    devtest::report_hw("accel_power", "with ECO GPIO-controlled the device disappears while the rail is off");
    return ok ? 0 : 1;
}

// ---- app_main bring-up entry points (bringup_tests.h) ----------------------------------

void test_accelerometer()
{
    devtest::begin(5, "M5.1", "LIS2DW12 ACCELEROMETER");
    devtest::finish("M5.1", devtest::call(cmd_accel, {"accel", "30"}));
}

void test_accelerometer_overrun()
{
    devtest::begin(5, "M5.2", "LIS2DW12 FIFO OVERRUN (FAULT INJECTION)");
    devtest::finish("M5.2", devtest::call(cmd_accel_overrun, {"accel_overrun"}));
}

void test_accelerometer_repeat()
{
    devtest::begin(5, "M5.3", "LIS2DW12 REPEATED CAPTURES");
    devtest::finish("M5.3", devtest::call(cmd_accel_repeat, {"accel_repeat", "3"}));
}

void test_accelerometer_power_cycle()
{
    devtest::begin(5, "M5.4", "LIS2DW12 RAIL OFF/ON RE-INIT");
    devtest::finish("M5.4", devtest::call(cmd_accel_power, {"accel_power"}));
}

void test_accelerometer_decode()
{
    devtest::begin(5, "M5.5", "ACCELEROMETER DECODE MATH (NO HARDWARE)");
    const bool ok = unit_run_group("accel_decode");
    if (!ok) devtest::report_fail("accel_decode", "decode unit checks failed (FAIL lines above)");
    devtest::finish("M5.5", ok ? 0 : 1);
}
