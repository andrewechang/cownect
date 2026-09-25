#include <cstdio>
#include <cstring>
#include "bringup_tests.h"
#include "devtest.h"
#include "devtest_common.h"
#include "esp_console.h"
#include "esp_log.h"
#include "sdkconfig.h"

namespace {
constexpr const char* TAG = "DEVTEST";

struct TestCommand {
    const char* name;
    int (*fn)(int, char**);
    const char* help;
};

// Names follow project instructions section 20 and the per-material command lists.
const TestCommand kCommands[] = {
    // Material 4
    {"board", cmd_board, "boot/self-test report (PSRAM, mode, rail command)"},
    {"gpio", cmd_gpio, "[seconds=15] pin map + BOOT/MODE_SW levels"},
    {"mode", cmd_mode, "[seconds] print debounced MODE_SW while moving PROG"},
    {"rail", cmd_rail, "on|off command SWITCHED_3V3 (ECO may force ON)"},
    {"adc_smoke", cmd_adc_smoke, "raw reads of GPIO1/GPIO2/GPIO5"},
    // Material 5
    {"accel", cmd_accel, "[seconds=30] WHO_AM_I, register readback, capture"},
    {"accel_overrun", cmd_accel_overrun, "deliberate FIFO overrun (fault injection)"},
    {"accel_repeat", cmd_accel_repeat, "[count=3] repeated 5 s captures"},
    {"accel_power", cmd_accel_power, "switched-rail off/on re-initialization"},
    // Material 6
    {"temperature", cmd_temperature, "[seconds=30] board + cow 1 Hz one-shot"},
    {"temp_board", cmd_temp_board, "[seconds=10] MCP9700 only"},
    {"temp_cow", cmd_temp_cow, "[seconds=10] MF58 only (unplug/short for fault tests)"},
    {"adc_conflict", cmd_adc_conflict, "ADC1 ownership conflict must be refused"},
    // Material 7
    {"gps", cmd_gps, "[seconds=10] UART/NMEA traffic test"},
    {"gps_discovery", cmd_gps_discovery, "[seconds=20] sentence identifiers"},
    {"gps_capture", cmd_gps_capture, "[seconds=30] fix retention"},
    {"gps_power", cmd_gps_power, "[max_s=60] VCC off/on, VBAT retained"},
    // Material 8
    {"microphone", cmd_microphone, "[seconds=30] standalone 32 kS/s capture"},
    {"mic_stream", cmd_mic_stream, "[seconds=3] ADC stream identification"},
    {"mic_overflow", cmd_mic_overflow, "deliberate slow consumer (fault injection)"},
    {"mic_power", cmd_mic_power, "switched-rail off/on re-initialization"},
    {"mic_dump", cmd_mic_dump, "[n=32] first/last n raw samples of last capture"},
    // Material 9
    {"capture", cmd_capture, "[seconds=5] integrated CaptureSession (radios off)"},
    {"adc_pattern", cmd_adc_pattern, "[seconds=5] MIC,TB,MIC,TC pattern test"},
    {"capture_repeat", cmd_capture_repeat, "[count=3] [seconds=5]"},
    {"capture_power", cmd_capture_power, "capture, rail off/on, capture"},
    // Material 10
    {"scheduler", cmd_scheduler, "[cycle_ms=20000] [capture_ms=5000] awake cycle"},
    {"cycle", cmd_cycle, "[cycle_ms=20000] [capture_ms=5000] cycle with NORMAL deep sleep"},
    {"cycle_repeat", cmd_cycle_repeat, "[cycle_ms] [capture_ms] [count=3] (deep sleep in NORMAL)"},
    {"sleep", cmd_sleep, "[seconds=10] timer deep sleep (NORMAL mode required)"},
    {"overrun", cmd_overrun, "forced overrun: 20 s cycle, 5 s capture, +18 s delay"},
    {"cycle_production", cmd_cycle_production, "one 120 s / 30 s cycle"},
    {"power_idle", cmd_power_idle, "short capture then rail command OFF"},
    // Material 11
    {"telemetry", cmd_telemetry, "[seconds=5] capture + TelemetryRecord"},
    // Material 12
    {"lora_spi", cmd_lora_spi, "SPI GetStatus after power-up (non-radiating)"},
    {"lora_reset", cmd_lora_reset, "NRESET pulse -> STDBY_RC (non-radiating)"},
    {"lora", cmd_lora, "non-radiating SPI/RESET/BUSY/status tests"},
    {"lora_config", cmd_lora_config, "RF profile + TX gate status"},
    {"lora_tx", cmd_lora_tx, "[count=10] [interval_ms=1000] PING (gated)"},
    {"lora_rx", cmd_lora_rx, "[seconds=30] receive test packets"},
    {"lora_ping", cmd_lora_ping, "[count=10] PING/ACK round trip (gated)"},
    {"lora_power", cmd_lora_power, "switched-rail off/on radio re-init"},
    // Material 13
    {"lora_full", cmd_lora_full, "[capture_s=30] full CaptureSession send-once (gated)"},
    {"full_serializer", cmd_full_serializer, "serializer fixture round trip (no RF)"},
    {"full_fragment", cmd_full_fragment, "[payload] fragment reconstruction (no RF)"},
    {"full_small", cmd_full_small, "[bytes=4096] synthetic stream transfer (gated)"},
    {"full_loss", cmd_full_loss, "[nth=5] [bytes=4096] deliberate loss (gated)"},
    {"full_overrun", cmd_full_overrun, "LORA_FULL beyond a 20 s test cycle (gated)"},
    // Material 14
    {"wifi_connect", cmd_wifi_connect, "join configured AP"},
    {"wifi_tcp", cmd_wifi_tcp, "short payload to Jetson"},
    {"wifi_small_upload", cmd_wifi_small_upload, "[bytes=65536] generated stream upload"},
    {"wifi_capture_upload", cmd_wifi_capture_upload, "[capture_s=30] real capture upload"},
    {"wifi_repeat", cmd_wifi_repeat, "[count=3] repeated 5 s capture uploads"},
    // Material 15
    {"hybrid_short", cmd_hybrid_short, "5 s capture -> LoRa telemetry -> Wi-Fi raw"},
    {"hybrid_full", cmd_hybrid_full, "30 s capture -> LoRa telemetry -> Wi-Fi raw"},
    {"hybrid_repeat", cmd_hybrid_repeat, "[count=3] short hybrid cycles"},
    // Pure-function tests and status
    {"unit", cmd_unit, "pure-function unit tests (no hardware)"},
    {"status", cmd_status, "configuration / CONFIG_NOT_SET status"},
};

void list_commands()
{
    std::printf("usage: test <name> [args]\n");
    for (const auto& c : kCommands) {
        std::printf("  %-20s %s\n", c.name, c.help);
    }
}

int test_dispatch(int argc, char** argv)
{
    if (argc < 2) {
        list_commands();
        return 0;
    }
    for (const auto& c : kCommands) {
        if (std::strcmp(argv[1], c.name) == 0) {
            const int rc = c.fn(argc - 1, argv + 1);
            std::printf("[test %s] exit=%d\n", c.name, rc);
            return rc;
        }
    }
    std::printf("unknown test '%s'\n", argv[1]);
    list_commands();
    return 1;
}
}  // namespace

esp_err_t devtest_start_console()
{
    esp_console_repl_t* repl = nullptr;
    esp_console_repl_config_t rc = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    rc.prompt = "cownect>";
    rc.task_stack_size = 16384;  // tests run in the REPL task
    rc.max_cmdline_length = 128;

    esp_err_t err;
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    esp_console_dev_usb_serial_jtag_config_t hw = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    err = esp_console_new_repl_usb_serial_jtag(&hw, &rc, &repl);
#elif CONFIG_ESP_CONSOLE_USB_CDC
    esp_console_dev_usb_cdc_config_t hw = ESP_CONSOLE_DEV_CDC_CONFIG_DEFAULT();
    err = esp_console_new_repl_usb_cdc(&hw, &rc, &repl);
#else
    esp_console_dev_uart_config_t hw = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    err = esp_console_new_repl_uart(&hw, &rc, &repl);
#endif
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "console init failed: %s", esp_err_to_name(err));
        return err;
    }
    esp_console_register_help_command();
    esp_console_cmd_t cmd = {};
    cmd.command = "test";
    cmd.help = "CowNect staged development tests. 'test' alone lists them.";
    cmd.hint = "<name> [args]";
    cmd.func = &test_dispatch;
    err = esp_console_cmd_register(&cmd);
    if (err != ESP_OK) {
        return err;
    }
    std::printf("\nDevelopment console ready. Type 'test' for the list.\n"
                "The 120 s scheduler is NOT running; start it explicitly (test scheduler / test cycle).\n");
    return esp_console_start_repl(repl);
}

// ---- app_main bring-up tools (bringup_tests.h) -------------------------------------------

void test_start_console()
{
    std::printf("\n========================================\nDEVELOPMENT CONSOLE\n"
                "========================================\n");
    devtest_resume_after_wake();  // finishes a console-started deep-sleep test, if one was pending
    const esp_err_t err = devtest_start_console();
    if (err != ESP_OK) {
        std::printf("RESULT: FAIL\nReason: console could not start (%s)\n", esp_err_to_name(err));
    }
}

void bringup_print_summary()
{
    cownect::devtest::print_summary();
}
