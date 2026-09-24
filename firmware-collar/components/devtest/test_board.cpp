// Material 4: board foundation / smoke tests.
#include <cstdio>
#include <cstring>
#include "board_adc.h"
#include "board_identity.h"
#include "board_mode.h"
#include "board_pins.h"
#include "board_power.h"
#include "board_self_test.h"
#include "bringup_tests.h"
#include "capture_session.h"
#include "cownect_config.h"
#include "devtest.h"
#include "devtest_common.h"
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "microphone_capture_driver.h"
#include "rtc_state.h"
#include "sensor_manager.h"
#include "sleep_manager.h"

using namespace cownect;

// Boot report (formerly printed by app_main at every boot).
static int boot_report()
{
    const data::RtcSchedulerState& rtc = data::rtc_state();
    std::printf("FW: %s (pinned ESP-IDF %s, running %s)\n", board_firmware_version(), config::PINNED_IDF_VERSION,
                esp_get_idf_version());
    std::printf("Reset reason: %s, wake cause: %s\n", power::reset_reason_name(esp_reset_reason()),
                power::wake_cause_name(esp_sleep_get_wakeup_cause()));
    std::printf("Retained RTC state: %s (cycle_seq=%u next_capture_id=%u last_sleep_req=%ums last_overrun=%ums)\n",
                data::rtc_state_valid_at_boot() ? "valid" : "reinitialized", static_cast<unsigned>(rtc.cycle_sequence),
                static_cast<unsigned>(rtc.next_capture_id), static_cast<unsigned>(rtc.last_sleep_requested_ms),
                static_cast<unsigned>(rtc.last_cycle_overrun_ms));
    std::printf("device_id: 0x%08llX (DEVELOPMENT DEFAULT)\n", static_cast<unsigned long long>(board_device_id()));
    std::printf("Operation mode: %s\n", operation_mode_name(board_get_operation_mode()));
    std::printf("Peripheral command: %s (physical rail not measured)\n",
                board_peripherals_commanded_enabled() ? "ON" : "OFF");
    devtest::report_pass("boot_report", "boot information printed");
    devtest::report_hw("boot_report", "reset reason / mode match what you did (power-on, PROG switch position)");
    return 0;
}

// Pin map + live levels of the inputs firmware can read without powering anything.
int cmd_gpio(int argc, char** argv)
{
    const uint32_t seconds = devtest::arg_u32(argc, argv, 1, 15);
    std::printf("Pin map: BOOT=%d TB_ADC=%d TC_ADC=%d PERIPH_EN=%d MIC_ADC=%d SDA=%d SCL=%d\n", board::PIN_BOOT,
                board::PIN_BOARD_TEMP_ADC, board::PIN_COW_TEMP_ADC, board::PIN_PERIPH_EN, board::PIN_MIC_ADC,
                board::PIN_I2C_SDA, board::PIN_I2C_SCL);
    std::printf("         LORA NSS=%d MOSI=%d SCK=%d MISO=%d RST=%d DIO1=%d BUSY=%d | MODE_SW=%d GPS RX=%d TX=%d\n",
                board::PIN_LORA_CS, board::PIN_LORA_MOSI, board::PIN_LORA_SCK, board::PIN_LORA_MISO,
                board::PIN_LORA_RST, board::PIN_LORA_DIO1, board::PIN_LORA_BUSY, board::PIN_MODE_SW,
                board::PIN_GPS_UART_RX, board::PIN_GPS_UART_TX);
    // BOOT has an external 10k pull-up (R21); configure as plain input to read it.
    gpio_config_t in = {};
    in.pin_bit_mask = 1ULL << board::PIN_BOOT;
    in.mode = GPIO_MODE_INPUT;
    if (gpio_config(&in) != ESP_OK) {
        devtest::report_fail("gpio", "could not configure GPIO0 (BOOT) as input");
        return 1;
    }
    std::printf("PERIPH_EN command: %s\n", board_peripherals_commanded_enabled() ? "ON" : "OFF");
    std::printf("Press BOOT and move the PROG switch now. Watching levels for %u s:\n", static_cast<unsigned>(seconds));
    int last_boot = -1;
    int last_mode = -1;
    for (uint32_t i = 0; i < seconds * 10; ++i) {
        const int boot = gpio_get_level(static_cast<gpio_num_t>(board::PIN_BOOT));
        const int mode = gpio_get_level(static_cast<gpio_num_t>(board::PIN_MODE_SW));
        if (boot != last_boot || mode != last_mode) {
            std::printf("  t=%4.1fs BOOT(GPIO0)=%d MODE_SW(GPIO15)=%d\n", i * 0.1, boot, mode);
            last_boot = boot;
            last_mode = mode;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    devtest::report_hw("gpio", "BOOT idle=1 pressed=0; MODE_SW PROGRAMMING=0 NORMAL=1; no chatter");
    return 0;
}

int cmd_board(int, char**)
{
    const PsramReport r = board_psram_self_test(config::MIC_CAPTURE_MAX_SAMPLES * sizeof(uint16_t));
    std::printf("PSRAM: %s initialized=%d size=%u heap_total=%u heap_free=%u test_alloc=%u ok=%d\n",
                r.pass ? "PASS" : "FAIL", r.initialized, static_cast<unsigned>(r.psram_size_bytes),
                static_cast<unsigned>(r.heap_total_bytes), static_cast<unsigned>(r.heap_free_bytes),
                static_cast<unsigned>(r.test_alloc_bytes), r.test_alloc_ok);
    std::printf("Mic capture buffer: %s\n", data::capture_storage_microphone_available() ? "allocated" : "UNAVAILABLE");
    std::printf("Operation mode: %s\n", operation_mode_name(board_get_operation_mode()));
    std::printf("Peripheral command: %s (physical rail not measured)\n",
                board_peripherals_commanded_enabled() ? "ON" : "OFF");
    if (!r.pass) {
        devtest::report_fail("board", "PSRAM verification failed (check module variant N16R8 / octal PSRAM config)");
        return 1;
    }
    if (!data::capture_storage_microphone_available()) {
        devtest::report_fail("board", "microphone capture buffer could not be allocated in PSRAM");
        return 1;
    }
    devtest::report_pass("board");
    devtest::report_hw("board", "PSRAM size reported as 8 MB");
    return 0;
}

int cmd_mode(int argc, char** argv)
{
    const uint32_t seconds = devtest::arg_u32(argc, argv, 1, 20);
    std::printf("Move the PROG switch now. Printing debounced mode every 500 ms for %u s.\n",
                static_cast<unsigned>(seconds));
    OperationMode last = OperationMode::UNSTABLE;
    for (uint32_t i = 0; i < seconds * 2; ++i) {
        const OperationMode m = board_get_operation_mode();
        if (i == 0 || m != last) {
            std::printf("  t=%4.1fs mode=%s\n", i * 0.5, operation_mode_name(m));
            last = m;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    devtest::report_hw("mode", "LOW position -> PROGRAMMING, HIGH -> NORMAL, no spurious transitions");
    return 0;
}

int cmd_rail(int argc, char** argv)
{
    if (argc >= 2 && std::strcmp(argv[1], "on") == 0) {
        devtest::rail_on();
    } else if (argc >= 2 && std::strcmp(argv[1], "off") == 0) {
        devtest::rail_off();
    } else {
        std::printf("usage: test rail on|off\n");
        return 1;
    }
    std::printf("PERIPH_EN command = %s\n", board_peripherals_commanded_enabled() ? "ON" : "OFF");
    devtest::report_hw("rail",
                       "ECO in GPIO-controlled position: LED1/SWITCHED_3V3 follows the command. "
                       "ECO forced-on: rail stays on regardless (not a firmware failure)");
    return 0;
}

int cmd_adc_smoke(int, char**)
{
    if (!devtest::ensure_idle("adc_smoke")) return 1;
    devtest::rail_on();
    sensors::SensorSet& set = sensors::sensor_set();
    int rc = 0;
    const char* why = "";
    // Temperatures through the real one-shot backend.
    if (set.oneshot_adc.init() == ESP_OK) {
        sensors::AdcReading b, c;
        const esp_err_t eb = set.oneshot_adc.read_board_temperature_channel(b);
        const esp_err_t ec = set.oneshot_adc.read_cow_temperature_channel(c);
        std::printf("GPIO1 board raw=%d mv=%d(%s) err=%s\n", b.raw, b.millivolts, b.millivolts_valid ? "cal" : "NO CAL",
                    esp_err_to_name(eb));
        std::printf("GPIO2 cow   raw=%d mv=%d(%s) err=%s\n", c.raw, c.millivolts, c.millivolts_valid ? "cal" : "NO CAL",
                    esp_err_to_name(ec));
        if (eb != ESP_OK || ec != ESP_OK) {
            rc = 1;
            why = "one-shot read of GPIO1/GPIO2 failed";
        }
        set.oneshot_adc.deinit();
    } else {
        rc = 1;
        why = "one-shot ADC1 init failed";
    }
    // Microphone through the real continuous driver (0.5 s).
    set.microphone.set_integrated_mode(false);
    if (set.microphone.init() == ESP_OK && set.microphone.start_capture() == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(500));
        set.microphone.stop_capture();
        const auto& s = set.microphone.stats();
        std::printf("GPIO5 mic samples=%u mean=%.1f min=%u max=%u\n", static_cast<unsigned>(s.samples_stored), s.raw_mean,
                    s.raw_min, s.raw_max);
        if (s.samples_stored == 0) {
            rc = 1;
            why = "continuous ADC returned no GPIO5 samples";
        }
    } else {
        rc = 1;
        why = "continuous ADC (microphone) init/start failed";
    }
    rc ? devtest::report_fail("adc_smoke", why) : devtest::report_pass("adc_smoke", "all three ADC paths returned readings");
    devtest::report_hw("adc_smoke", "readings change plausibly with stimulus (warm sensor, unplug probe, sound)");
    return rc;
}

// ---- app_main bring-up entry points (bringup_tests.h) ----------------------------------

void test_boot_report()
{
    devtest::begin(4, "M4.1", "BOOT REPORT");
    devtest::finish("M4.1", boot_report());
}

void test_psram()
{
    devtest::begin(4, "M4.2", "PSRAM + CAPTURE BUFFER");
    devtest::finish("M4.2", devtest::call(cmd_board, {"board"}));
}

void test_board_gpio()
{
    devtest::begin(4, "M4.3", "BOARD GPIO (BOOT / MODE_SW / PERIPH_EN)");
    devtest::finish("M4.3", devtest::call(cmd_gpio, {"gpio", "15"}), devtest::Judge::OPERATOR);
}

void test_operation_mode()
{
    devtest::begin(4, "M4.4", "OPERATION MODE (PROG SWITCH, DEBOUNCED)");
    devtest::finish("M4.4", devtest::call(cmd_mode, {"mode", "20"}), devtest::Judge::OPERATOR);
}

void test_peripheral_enable()
{
    devtest::begin(4, "M4.5", "PERIPHERAL ENABLE (PERIPH_EN / SWITCHED_3V3)");
    std::printf("PERIPH_EN commanded ON for 10 s - measure SWITCHED_3V3 / watch LED1 now\n");
    int rc = devtest::call(cmd_rail, {"rail", "on"});
    vTaskDelay(pdMS_TO_TICKS(10000));
    std::printf("PERIPH_EN commanded OFF for 10 s - SWITCHED_3V3 should drop (unless ECO forces it on)\n");
    rc |= devtest::call(cmd_rail, {"rail", "off"});
    vTaskDelay(pdMS_TO_TICKS(10000));
    devtest::finish("M4.5", rc, devtest::Judge::OPERATOR);
}

void test_adc_smoke()
{
    devtest::begin(4, "M4.6", "ADC SMOKE (GPIO1 / GPIO2 / GPIO5)");
    devtest::finish("M4.6", devtest::call(cmd_adc_smoke, {"adc_smoke"}));
}

void test_config_status()
{
    devtest::begin(4, "M4.7", "CONFIGURATION STATUS (CONFIG_NOT_SET)");
    devtest_print_config_status();
    devtest::report_hw("config_status", "items shown as CONFIG_NOT_SET block only their own feature");
    devtest::finish("M4.7", 0);
}
