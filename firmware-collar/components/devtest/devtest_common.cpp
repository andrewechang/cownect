#include "devtest_common.h"

#include <cstdio>
#include <cstdlib>
#include "board_adc.h"
#include "board_identity.h"
#include "board_power.h"
#include "communication_manager.h"
#include "cownect_config.h"
#include "cownect_err.h"
#include "devtest.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lora_config.h"
#include "lora_full_transfer.h"
#include "power_manager.h"
#include "prototype_wifi_config.h"
#include "sensor_manager.h"
#include "sx1262_adapter.h"

namespace cownect::devtest {
namespace {

// Remembered by the report_* helpers so the bring-up result banner can state the reason.
char s_reason[400];
char s_hw[400];

struct Tally {
    uint32_t run, pass, fail, blocked, needs_hw;
};
Tally s_tally = {};

void remember(char* dst, const char* text)
{
    if (text != nullptr && text[0] != '\0') {
        std::snprintf(dst, sizeof(s_reason), "%s", text);
    }
}

constexpr const char* kLine = "========================================";

}  // namespace

void report_pass(const char* test, const char* detail)
{
    std::printf("[%s] SOFTWARE_RESULT=PASS %s\n", test, detail);
}

void report_fail(const char* test, const char* detail)
{
    std::printf("[%s] SOFTWARE_RESULT=FAIL %s\n", test, detail);
    remember(s_reason, detail);
}

void report_blocked(const char* test, esp_err_t err, const char* detail)
{
    std::printf("[%s] SOFTWARE_RESULT=BLOCKED (%s) %s\n", test, cownect_err_name(err), detail ? detail : "");
    char buf[sizeof(s_reason)];
    std::snprintf(buf, sizeof(buf), "%s: %s", cownect_err_name(err), detail ? detail : "");
    remember(s_reason, buf);
}

void report_hw(const char* test, const char* operator_checks)
{
    std::printf("[%s] HARDWARE_VALIDATION=NEEDS_HARDWARE_VALIDATION: %s\n", test, operator_checks);
    remember(s_hw, operator_checks);
}

void begin(int material, const char* id, const char* title, bool rf_tx)
{
    s_reason[0] = '\0';
    s_hw[0] = '\0';
    if (material > 0) {
        std::printf("\n%s\nMATERIAL %d TEST %s: %s\n%s\n", kLine, material, id, title, kLine);
    } else {
        std::printf("\n%s\nBRING-UP TOOL %s: %s\n%s\n", kLine, id, title, kLine);
    }
    if (rf_tx) {
        rf_tx_warning();
    }
}

void finish(const char* id, int rc, Judge judge)
{
    s_tally.run++;
    std::printf("----------------------------------------\n");
    if (rc == 2) {
        s_tally.blocked++;
        std::printf("%s RESULT: BLOCKED\nReason: %s\n", id, s_reason[0] ? s_reason : "CONFIG_NOT_SET (see log above)");
    } else if (rc != 0) {
        s_tally.fail++;
        std::printf("%s RESULT: FAIL\nReason: %s\n", id, s_reason[0] ? s_reason : "see log above");
    } else if (judge == Judge::OPERATOR) {
        s_tally.needs_hw++;
        std::printf("%s RESULT: NEEDS_HARDWARE_VALIDATION\nOperator must confirm: %s\n", id,
                    s_hw[0] ? s_hw : "observe the values printed above");
    } else {
        s_tally.pass++;
        std::printf("%s RESULT: PASS\n", id);
        if (s_hw[0]) {
            std::printf("Hardware validation: NEEDS_HARDWARE_VALIDATION - %s\n", s_hw);
        }
    }
    std::printf("%s\n\n", kLine);
}

int call(int (*fn)(int, char**), std::initializer_list<const char*> args)
{
    constexpr size_t kMaxArgs = 6;
    char storage[kMaxArgs][24] = {};
    char* argv[kMaxArgs] = {};
    int argc = 0;
    for (const char* a : args) {
        if (argc == static_cast<int>(kMaxArgs)) break;
        std::snprintf(storage[argc], sizeof(storage[argc]), "%s", a);
        argv[argc] = storage[argc];
        argc++;
    }
    return fn(argc, argv);
}

void rf_tx_warning()
{
    std::printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n"
                "!! VERIFY EXTERNAL ANTENNA IS CONNECTED BEFORE RF TX\n"
                "!! (TX still requires the complete RF profile + COWNECT_LORA_ANTENNA_VERIFIED)\n"
                "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
}

void rf_tx_countdown(uint32_t seconds)
{
    rf_tx_warning();
    for (uint32_t s = seconds; s > 0; --s) {
        std::printf("RF TX starts in %u s - reset the board now if the antenna is not connected\n",
                    static_cast<unsigned>(s));
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void print_summary()
{
    std::printf("\n%s\nBRING-UP SUMMARY\n%s\n", kLine, kLine);
    if (s_tally.run == 0) {
        std::printf("No bring-up test ran in this boot. To run one, uncomment ONE test call in\n"
                    "main/app_main.cpp, rebuild and flash.\n");
    } else {
        std::printf("tests run=%u PASS=%u FAIL=%u BLOCKED=%u NEEDS_HARDWARE_VALIDATION=%u\n",
                    static_cast<unsigned>(s_tally.run), static_cast<unsigned>(s_tally.pass),
                    static_cast<unsigned>(s_tally.fail), static_cast<unsigned>(s_tally.blocked),
                    static_cast<unsigned>(s_tally.needs_hw));
        rail_off();  // leave the board in the safe idle state (sensors stopped, rail commanded OFF)
        std::printf("SWITCHED_3V3 commanded OFF after the tests.\n");
    }
    std::printf("%s\n\n", kLine);
}

uint32_t arg_u32(int argc, char** argv, int index, uint32_t default_value)
{
    if (index >= argc) {
        return default_value;
    }
    char* end = nullptr;
    const unsigned long v = std::strtoul(argv[index], &end, 0);
    return (end != argv[index] && *end == '\0') ? static_cast<uint32_t>(v) : default_value;
}

esp_err_t rail_on()
{
    return power::power_manager().peripherals_on();
}

void rail_off()
{
    sensors::sensor_manager().ensure_stopped();
    power::power_manager().peripherals_off();
    sensors::sensor_manager().notify_peripheral_power_lost();
    radio::lora_radio().mark_power_lost();
}

bool ensure_idle(const char* test)
{
    if (sensors::sensor_manager().is_capturing()) {
        report_fail(test, "refused: a capture is already active");
        return false;
    }
    return true;
}

}  // namespace cownect::devtest

using namespace cownect;

void devtest_print_config_status()
{
    std::printf("---- CowNect configuration status ----\n");
    std::printf("device_id=0x%08llX (DEVELOPMENT DEFAULT)\n", static_cast<unsigned long long>(board_device_id()));
    std::printf("ADC atten=%s bitwidth=12 (PROVISIONAL, NEEDS_HARDWARE_VALIDATION)\n", board::adc_attenuation_name());
    std::printf("periph stabilization=%ums, mode debounce=%ums (DEVELOPMENT DEFAULT)\n",
                static_cast<unsigned>(config::PERIPH_STABILIZE_MS), static_cast<unsigned>(config::MODE_DEBOUNCE_MS));

    radio::LoraRfProfile p;
    radio::LoraConfigReport rep;
    const esp_err_t lora = radio::lora_load_rf_profile(p, rep);
    std::printf("LoRa RF profile: %s%s%s\n", lora == ESP_OK ? "configured" : "CONFIG_NOT_SET",
                lora == ESP_OK ? "" : " missing=", lora == ESP_OK ? "" : rep.missing);
    const char* reason = nullptr;
    radio::lora_tx_gate(&reason);
    std::printf("LoRa TX gate: %s\n", reason);

    lorafull::LoraFullConfig lf;
    const char* missing = nullptr;
    const esp_err_t lfe = lorafull::lora_full_load_config(lf, &missing);
    std::printf("LORA_FULL: %s %s\n", lfe == ESP_OK ? "configured" : cownect_err_name(lfe), missing ? missing : "");

    wifi::PrototypeWifiConfig w;
    const esp_err_t we = wifi::prototype_wifi_config_load(w, &missing);
    std::printf("Wi-Fi/Jetson: %s %s\n", we == ESP_OK ? "configured" : "CONFIG_NOT_SET", missing ? missing : "");

    comm::CommunicationMode mode = comm::CommunicationMode::HYBRID_LORA_WIFI;  // overwritten if enabled
    if (comm::communication_mode_from_config(mode)) {
        std::printf("Scheduler communication: %s\n", comm::communication_mode_name(mode));
    } else {
        std::printf("Scheduler communication: DISABLED (menuconfig)\n");
    }
    std::printf("Absolute time source: CONFIG_NOT_SET (monotonic time + capture_id only)\n");
    std::printf("--------------------------------------\n");
}

int cmd_status(int, char**)
{
    devtest_print_config_status();
    return 0;
}
