// Material 12: Ra-01SH / SX1262 tests. Non-radiating tests always run; anything that transmits
// goes through the RF TX gate (configured profile + operator antenna confirmation).
#include <cstdio>
#include "board_identity.h"
#include "bringup_tests.h"
#include "cownect_err.h"
#include "devtest_common.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lora_config.h"
#include "lora_test_packet.h"
#include "sx1262_adapter.h"

using namespace cownect;

namespace {

void print_status(const char* label)
{
    radio::LoraChipStatus st;
    const esp_err_t err = radio::lora_radio().read_status(st);
    std::printf("[LORA] %s: status=%s chip_mode=%s(%u) cmd_status=%u device_errors=0x%04X busy=%d last_busy_wait=%uus\n",
                label, cownect_err_name(err), radio::lora_chip_mode_name(st.chip_mode), st.chip_mode, st.cmd_status,
                st.device_errors, st.busy_level, static_cast<unsigned>(st.last_busy_wait_us));
}

void print_stats()
{
    const auto& s = radio::lora_radio().stats();
    std::printf("[LORA] tx_started=%u tx_done=%u tx_timeout=%u rx_done=%u rx_timeout=%u crc_err=%u spi_err=%u "
                "busy_timeout=%u resets=%u irq=%u unexpected_irq=%u malformed=%u seq_gap=%u\n",
                static_cast<unsigned>(s.tx_started), static_cast<unsigned>(s.tx_done), static_cast<unsigned>(s.tx_timeout),
                static_cast<unsigned>(s.rx_done), static_cast<unsigned>(s.rx_timeout), static_cast<unsigned>(s.rx_crc_error),
                static_cast<unsigned>(s.spi_error_count), static_cast<unsigned>(s.busy_timeout_count),
                static_cast<unsigned>(s.reset_count), static_cast<unsigned>(s.irq_count),
                static_cast<unsigned>(s.unexpected_irq_count), static_cast<unsigned>(s.malformed_packet_count),
                static_cast<unsigned>(s.sequence_gap_count));
}

// Returns 0 when TX may proceed; otherwise prints the refusal (test 12.4 RF preflight).
int tx_preflight(const char* test)
{
    const char* reason = nullptr;
    const esp_err_t gate = radio::lora_tx_gate(&reason);
    if (gate != ESP_OK) {
        devtest::report_blocked(test, gate, reason);
        std::printf("[%s] REFUSE: RF profile / antenna confirmation incomplete. Checklist: external antenna attached, "
                    "Ra-01SH + Heltec band variants confirmed, identical RF profile on both, low bench TX power.\n",
                    test);
        return 2;
    }
    devtest::rail_on();
    const esp_err_t err = radio::lora_radio_prepare_from_config();
    if (err != ESP_OK) {
        char why[80];
        std::snprintf(why, sizeof(why), "applying the RF profile to the SX1262 failed (%s)", cownect_err_name(err));
        devtest::report_fail(test, why);
        return 1;
    }
    devtest::rf_tx_countdown();
    return 0;
}

bool hardware_ready(const char* test)
{
    devtest::rail_on();
    const esp_err_t err = radio::lora_radio().init_hardware();
    std::printf("[LORA] GPIO/SPI init: %s (NSS=9 MOSI=10 SCK=11 MISO=12 RST=13 DIO1=14 BUSY=21)\n", cownect_err_name(err));
    if (err != ESP_OK) {
        char why[80];
        std::snprintf(why, sizeof(why), "ESP32 GPIO/SPI2 bus init failed (%s)", cownect_err_name(err));
        devtest::report_fail(test, why);
        return false;
    }
    return true;
}

// Chip modes 2..6 (STBY_RC..TX) are the only valid values; 0/1/7 means no real status byte.
bool chip_mode_plausible(uint8_t mode)
{
    return mode >= 2 && mode <= 6;
}

}  // namespace

// Test 12.1: SPI command/status path without a reset pulse (non-radiating). After the rail
// power-up the SX1262 finishes its own POR calibration and should answer GetStatus in STDBY_RC.
int cmd_lora_spi(int, char**)
{
    if (!hardware_ready("lora_spi")) return 1;
    std::printf("[LORA] BUSY=%d (expected 0 once the chip is idle)\n", radio::lora_radio().busy_level());
    radio::LoraChipStatus st;
    const esp_err_t err = radio::lora_radio().read_status(st);
    print_status("GetStatus over SPI");
    if (err != ESP_OK) {
        char why[112];
        std::snprintf(why, sizeof(why), "GetStatus/GetDeviceErrors over SPI failed (%s) - check NSS/SCK/MOSI/MISO, BUSY",
                      cownect_err_name(err));
        devtest::report_fail("lora_spi", why);
        return 1;
    }
    if (!chip_mode_plausible(st.chip_mode)) {
        char why[96];
        std::snprintf(why, sizeof(why), "implausible status byte (chip_mode=%u) - MISO stuck or chip not powered",
                      st.chip_mode);
        devtest::report_fail("lora_spi", why);
        return 1;
    }
    devtest::report_pass("lora_spi", "SX1262 answered GetStatus with a valid chip mode (nothing transmitted)");
    return 0;
}

// Test 12.2: NRESET pulse, bounded BUSY release, chip back in STDBY_RC (non-radiating).
int cmd_lora_reset(int, char**)
{
    if (!hardware_ready("lora_reset")) return 1;
    radio::Sx1262Adapter& r = radio::lora_radio();
    const int64_t t0 = esp_timer_get_time();
    esp_err_t err = r.hardware_reset();
    std::printf("[LORA] reset: %s total=%lldus\n", cownect_err_name(err), static_cast<long long>(esp_timer_get_time() - t0));
    if (err != ESP_OK) {
        devtest::report_fail("lora_reset", "BUSY did not clear after the NRESET pulse (GPIO13) - check rail/RST/BUSY");
        return 1;
    }
    radio::LoraChipStatus st;
    err = r.read_status(st);
    print_status("after reset");
    if (err != ESP_OK || st.chip_mode != 2) {
        devtest::report_fail("lora_reset", "chip not in STDBY_RC after reset");
        return 1;
    }
    devtest::report_pass("lora_reset", "reset pulse -> BUSY released -> STDBY_RC");
    devtest::report_hw("lora_reset", "reset/BUSY timing plausible (compare total us with the SX1262 datasheet)");
    return 0;
}

// lora_run_spi_test + lora_run_reset_busy_test + lora_run_standby_test (tests 12.1-12.3).
int cmd_lora(int, char**)
{
    devtest::rail_on();
    radio::Sx1262Adapter& r = radio::lora_radio();
    esp_err_t err = r.init_hardware();
    std::printf("[LORA] GPIO/SPI init: %s (NSS=9 MOSI=10 SCK=11 MISO=12 RST=13 DIO1=14 BUSY=21)\n", cownect_err_name(err));
    if (err != ESP_OK) {
        devtest::report_fail("lora", "GPIO/SPI init");
        return 1;
    }
    std::printf("[LORA] BUSY before reset=%d\n", r.busy_level());
    const int64_t t0 = esp_timer_get_time();
    err = r.hardware_reset();
    std::printf("[LORA] reset: %s total=%lldus\n", cownect_err_name(err), static_cast<long long>(esp_timer_get_time() - t0));
    if (err != ESP_OK) {
        devtest::report_fail("lora", "BUSY did not clear after reset - check rail/SPI/reset before continuing");
        return 1;
    }
    print_status("after reset");
    err = r.standby_without_profile();
    print_status("after SetStandby(RC)");
    print_stats();
    const bool ok = err == ESP_OK;
    char why[80];
    std::snprintf(why, sizeof(why), "SetStandby(RC) command failed (%s)", cownect_err_name(err));
    ok ? devtest::report_pass("lora", "SPI command/status path OK, radio in standby (nothing transmitted)")
       : devtest::report_fail("lora", why);
    devtest::report_hw("lora", "BUSY timing plausible; TXEN/RXEN left unconnected still to be validated with RF");
    return ok ? 0 : 1;
}

int cmd_lora_config(int, char**)
{
    radio::LoraRfProfile p;
    radio::LoraConfigReport rep;
    esp_err_t err = radio::lora_load_rf_profile(p, rep);
    std::printf("[LORA] RF profile: %s %s\n", cownect_err_name(err), rep.missing);
    const char* reason = nullptr;
    radio::lora_tx_gate(&reason);
    std::printf("[LORA] TX gate: %s\n", reason);
    if (err != ESP_OK) {
        char why[sizeof(rep.missing) + 32];
        std::snprintf(why, sizeof(why), "LoRa RF profile missing %s", rep.missing);
        devtest::report_blocked("lora_config", err, why);
        return 2;
    }
    // Profile complete: apply it to the chip (reset, calibrate, modulation). Non-radiating.
    devtest::rail_on();
    err = radio::lora_radio_prepare_from_config();
    print_status("after profile apply");
    if (err != ESP_OK) {
        char why[80];
        std::snprintf(why, sizeof(why), "applying the RF profile to the SX1262 failed (%s)", cownect_err_name(err));
        devtest::report_fail("lora_config", why);
        return 1;
    }
    devtest::report_pass("lora_config", "RF profile valid and applied (nothing transmitted)");
    devtest::report_hw("lora_config", "profile identical on the Heltec gateway; band matches the Ra-01SH variant");
    return 0;
}

// lora_run_tx_test (test 12.5)
int cmd_lora_tx(int argc, char** argv)
{
    int rc = tx_preflight("lora_tx");
    if (rc != 0) return rc;
    const uint32_t count = devtest::arg_u32(argc, argv, 1, 10);
    const uint32_t interval = devtest::arg_u32(argc, argv, 2, 1000);
    radio::lora_radio().reset_stats();
    uint32_t ok = 0;
    for (uint32_t seq = 1; seq <= count; ++seq) {
        radio::LoraTestPacket p = {radio::LORA_TEST_VERSION, radio::LoraTestType::PING, board_device_id(), seq, 0};
        uint8_t buf[radio::LORA_TEST_PACKET_BYTES];
        const size_t n = radio::lora_test_packet_encode(p, buf, sizeof(buf));
        uint32_t ms = 0;
        const esp_err_t err = radio::lora_radio().send_blocking(buf, n, ms);
        std::printf("[LORA] TX seq=%u bytes=%u %s elapsed=%ums\n", static_cast<unsigned>(seq), static_cast<unsigned>(n),
                    err == ESP_OK ? "TX_DONE" : cownect_err_name(err), static_cast<unsigned>(ms));
        ok += err == ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(interval));
    }
    print_stats();
    rc = ok == count ? 0 : 1;
    char why[64];
    std::snprintf(why, sizeof(why), "only %u of %u packets reached TX_DONE", static_cast<unsigned>(ok),
                  static_cast<unsigned>(count));
    rc ? devtest::report_fail("lora_tx", why) : devtest::report_pass("lora_tx", "all TX_DONE (sender side only)");
    devtest::report_hw("lora_tx", "Heltec must log every sequence with RSSI/SNR; receiver completeness is separate");
    return rc;
}

// lora_run_rx_test (non-radiating, needs profile)
int cmd_lora_rx(int argc, char** argv)
{
    devtest::rail_on();
    esp_err_t err = radio::lora_radio_prepare_from_config();
    if (err == COWNECT_ERR_NOT_CONFIGURED) {
        devtest::report_blocked("lora_rx", err, "LoRa RF profile CONFIG_NOT_SET");
        return 2;
    }
    if (err != ESP_OK) {
        char why[80];
        std::snprintf(why, sizeof(why), "applying the RF profile to the SX1262 failed (%s)", cownect_err_name(err));
        devtest::report_fail("lora_rx", why);
        return 1;
    }
    const uint32_t seconds = devtest::arg_u32(argc, argv, 1, 30);
    std::printf("[LORA] listening %u s - start the Heltec test transmitter now\n", static_cast<unsigned>(seconds));
    radio::Sx1262Adapter& r = radio::lora_radio();
    const int64_t end = esp_timer_get_time() + static_cast<int64_t>(seconds) * 1000000;
    uint32_t last_seq = 0;
    uint32_t received = 0;
    while (esp_timer_get_time() < end) {
        const uint32_t window = static_cast<uint32_t>((end - esp_timer_get_time()) / 1000) + 1;
        if (r.start_receive(window) != ESP_OK) break;
        while (r.rx_in_progress()) r.wait_for_event(100);
        uint8_t buf[255];
        radio::LoraPacketRx meta = {};
        if (r.take_last_rx(buf, sizeof(buf), meta)) {
            radio::LoraTestPacket p = {};
            if (radio::lora_test_packet_decode(buf, meta.length, p)) {
                if (last_seq != 0 && p.sequence != last_seq + 1) r.note_sequence_gap();
                last_seq = p.sequence;
                received++;
                std::printf("[LORA] RX t=%lldms seq=%u dev=0x%llX len=%u rssi=%d snr=%d\n",
                            static_cast<long long>((esp_timer_get_time() - (end - seconds * 1000000LL)) / 1000),
                            static_cast<unsigned>(p.sequence), static_cast<unsigned long long>(p.device_id),
                            static_cast<unsigned>(meta.length), meta.rssi_dbm, meta.snr_db);
            } else {
                r.note_malformed_packet();
                std::printf("[LORA] RX malformed len=%u rssi=%d\n", static_cast<unsigned>(meta.length), meta.rssi_dbm);
            }
        }
    }
    print_stats();
    std::printf("[LORA] valid test packets received: %u\n", static_cast<unsigned>(received));
    devtest::report_hw("lora_rx", "received count / RSSI / SNR match what the Heltec transmitted");
    return 0;
}

// lora_run_ping_ack_test (test 12.6)
int cmd_lora_ping(int argc, char** argv)
{
    int rc = tx_preflight("lora_ping");
    if (rc != 0) return rc;
    radio::LoraDriverConfig d;
    radio::LoraConfigReport rep;
    radio::lora_load_driver_config(d, rep);
    if (!d.rx_timeout_configured) {
        devtest::report_blocked("lora_ping", COWNECT_ERR_NOT_CONFIGURED, "LoRa RX_TIMEOUT_MS");
        return 2;
    }
    const uint32_t count = devtest::arg_u32(argc, argv, 1, 10);
    radio::Sx1262Adapter& r = radio::lora_radio();
    uint32_t acked = 0;
    for (uint32_t seq = 1; seq <= count; ++seq) {
        radio::LoraTestPacket p = {radio::LORA_TEST_VERSION, radio::LoraTestType::PING, board_device_id(), seq, 0};
        uint8_t buf[255];
        const size_t n = radio::lora_test_packet_encode(p, buf, sizeof(buf));
        const int64_t t0 = esp_timer_get_time();
        uint32_t ms = 0;
        if (r.send_blocking(buf, n, ms) != ESP_OK || r.start_receive(d.rx_operation_timeout_ms) != ESP_OK) {
            std::printf("[LORA] seq=%u TX/RX start failed\n", static_cast<unsigned>(seq));
            continue;
        }
        while (r.rx_in_progress()) r.wait_for_event(100);
        radio::LoraPacketRx meta = {};
        radio::LoraTestPacket ack = {};
        if (r.take_last_rx(buf, sizeof(buf), meta) && radio::lora_test_packet_decode(buf, meta.length, ack) &&
            ack.type == radio::LoraTestType::ACK && ack.sequence == seq) {
            acked++;
            std::printf("[LORA] seq=%u ACK rtt=%lldms rssi=%d snr=%d\n", static_cast<unsigned>(seq),
                        static_cast<long long>((esp_timer_get_time() - t0) / 1000), meta.rssi_dbm, meta.snr_db);
        } else {
            std::printf("[LORA] seq=%u no matching ACK\n", static_cast<unsigned>(seq));
        }
    }
    print_stats();
    std::printf("[LORA] acked=%u/%u\n", static_cast<unsigned>(acked), static_cast<unsigned>(count));
    char why[64];
    std::snprintf(why, sizeof(why), "only %u of %u PINGs were ACKed", static_cast<unsigned>(acked),
                  static_cast<unsigned>(count));
    acked == count ? devtest::report_pass("lora_ping", "every PING ACKed") : devtest::report_fail("lora_ping", why);
    return acked == count ? 0 : 1;
}

// lora_run_power_cycle_test (test 12.8) - non-radiating
int cmd_lora_power(int, char**)
{
    int rc = cmd_lora(0, nullptr);
    devtest::rail_off();
    vTaskDelay(pdMS_TO_TICKS(1000));
    rc |= cmd_lora(0, nullptr);
    rc ? devtest::report_fail("lora_power", "radio did not re-initialize after the rail off/on cycle")
       : devtest::report_pass("lora_power", "radio re-initialized after rail cycle");
    return rc;
}

// ---- app_main bring-up entry points (bringup_tests.h) ----------------------------------

void test_lora_spi()
{
    devtest::begin(12, "M12.1", "SX1262 SPI STATUS (NON-RADIATING)");
    devtest::finish("M12.1", devtest::call(cmd_lora_spi, {"lora_spi"}));
}

void test_lora_reset()
{
    devtest::begin(12, "M12.2", "SX1262 RESET (NON-RADIATING)");
    devtest::finish("M12.2", devtest::call(cmd_lora_reset, {"lora_reset"}));
}

void test_lora_busy()
{
    devtest::begin(12, "M12.3", "SX1262 BUSY HANDSHAKE + STANDBY (NON-RADIATING)");
    devtest::finish("M12.3", devtest::call(cmd_lora, {"lora"}));
}

void test_lora_configuration()
{
    devtest::begin(12, "M12.4", "LORA RF PROFILE / TX GATE (NON-RADIATING)");
    devtest::finish("M12.4", devtest::call(cmd_lora_config, {"lora_config"}));
}

void test_lora_tx()
{
    devtest::begin(12, "M12.5", "LORA TX: 10 PING PACKETS", true);
    devtest::finish("M12.5", devtest::call(cmd_lora_tx, {"lora_tx", "10", "1000"}));
}

void test_lora_rx()
{
    devtest::begin(12, "M12.6", "LORA RX: 30 s RECEIVE WINDOW");
    devtest::finish("M12.6", devtest::call(cmd_lora_rx, {"lora_rx", "30"}), devtest::Judge::OPERATOR);
}

void test_lora_ping()
{
    devtest::begin(12, "M12.7", "LORA PING / ACK ROUND TRIP", true);
    devtest::finish("M12.7", devtest::call(cmd_lora_ping, {"lora_ping", "10"}));
}

void test_lora_power_cycle()
{
    devtest::begin(12, "M12.8", "SX1262 RAIL OFF/ON RE-INIT (NON-RADIATING)");
    devtest::finish("M12.8", devtest::call(cmd_lora_power, {"lora_power"}));
}
