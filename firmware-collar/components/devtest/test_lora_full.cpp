// Material 13: LORA_FULL tests. Serializer/fragment tests need no RF; transfers are gated.
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include "board_identity.h"
#include "bringup_tests.h"
#include "capture_session.h"
#include "capture_stream_encoder.h"
#include "communication_manager.h"
#include "cownect_err.h"
#include "cycle_scheduler.h"
#include "devtest_common.h"
#include "esp_heap_caps.h"
#include "lora_config.h"
#include "sensor_manager.h"
#include "synthetic_stream.h"

using namespace cownect;

// Implemented in test_unit.cpp (shared with `test unit`).
bool unit_capture_stream_roundtrip();
bool unit_fragment_reconstruction(uint16_t payload_bytes);

namespace {

void print_stats(const lorafull::LoraFullTransferStats& s)
{
    std::printf("[LORA_FULL] capture=%u bytes=%u payload=%u fragments=%u tx_started=%u tx_done=%u failed=%u "
                "(idx %u) retries=%u ack=%u ack_timeouts=%u radio_err=%u comm_ms=%u crc32=%s0x%08lX "
                "sender_complete=%d\n",
                static_cast<unsigned>(s.capture_id), static_cast<unsigned>(s.stream_bytes),
                static_cast<unsigned>(s.fragment_payload_bytes), static_cast<unsigned>(s.fragment_count),
                static_cast<unsigned>(s.fragments_tx_started), static_cast<unsigned>(s.fragments_tx_done),
                static_cast<unsigned>(s.fragments_failed), static_cast<unsigned>(s.failed_fragment_index),
                static_cast<unsigned>(s.retry_count), static_cast<unsigned>(s.ack_received),
                static_cast<unsigned>(s.ack_timeout_count), static_cast<unsigned>(s.radio_error_count),
                static_cast<unsigned>(s.communication_time_ms), s.stream_crc32_present ? "" : "(none) ",
                static_cast<unsigned long>(s.stream_crc32), s.transfer_complete_sender_side);
}

int gated_prepare(const char* test)
{
    lorafull::LoraFullConfig cfg;
    const char* missing = nullptr;
    esp_err_t err = lorafull::lora_full_load_config(cfg, &missing);
    if (err != ESP_OK) {
        char why[160];
        std::snprintf(why, sizeof(why), "LORA_FULL %s", missing ? missing : "configuration");
        devtest::report_blocked(test, err, why);
        return 2;
    }
    const char* reason = nullptr;
    err = radio::lora_tx_gate(&reason);
    if (err != ESP_OK) {
        char why[160];
        std::snprintf(why, sizeof(why), "LoRa TX gate: %s", reason);
        devtest::report_blocked(test, err, why);
        return 2;
    }
    devtest::rail_on();
    err = radio::lora_radio_prepare_from_config();
    if (err != ESP_OK) {
        char why[80];
        std::snprintf(why, sizeof(why), "applying the RF profile to the SX1262 failed (%s)", cownect_err_name(err));
        devtest::report_fail(test, why);
        return 1;
    }
    devtest::rf_tx_countdown();
    return 0;
}

int synthetic_transfer(const char* test, uint32_t bytes, uint32_t skip_nth)
{
    int rc = gated_prepare(test);
    if (rc != 0) return rc;
    codec::SyntheticStream stream(bytes, 0x5A);
    lorafull::LoraFullTransfer& x = comm::lora_full_transfer();
    x.set_skip_every_nth_for_test(skip_nth);
    esp_err_t err = x.begin_stream(stream, board_device_id(), 0xFFFF0000u | (bytes & 0xFFFF));
    if (err == ESP_OK) err = x.run_to_completion();
    x.set_skip_every_nth_for_test(0);
    print_stats(x.stats());
    if (err != ESP_OK) {
        char why[80];
        std::snprintf(why, sizeof(why), "LORA_FULL transfer incomplete on the sender side (%s)", cownect_err_name(err));
        devtest::report_fail(test, why);
    } else {
        devtest::report_pass(test, "sender side complete");
    }
    devtest::report_hw(test, "compare receiver unique/missing/duplicate counts; sender TX_DONE != receiver complete");
    return err == ESP_OK ? 0 : 1;
}

}  // namespace

// lora_full_run_serializer_test (test 13.1) - no PCB/RF needed
int cmd_full_serializer(int, char**)
{
    const bool ok = unit_capture_stream_roundtrip();
    ok ? devtest::report_pass("full_serializer", "deterministic fixture round trip")
       : devtest::report_fail("full_serializer", "capture stream encode/decode round trip mismatch (FAIL lines above)");
    return ok ? 0 : 1;
}

// lora_full_run_fragment_test (test 13.2) - no RF
int cmd_full_fragment(int argc, char** argv)
{
    const uint32_t payload = devtest::arg_u32(argc, argv, 1, 0);
    bool ok = true;
    if (payload != 0) {
        ok = unit_fragment_reconstruction(static_cast<uint16_t>(payload));
    } else {
        for (uint16_t p : {1, 7, 64, 150, 215}) ok = ok && unit_fragment_reconstruction(p);
    }
    ok ? devtest::report_pass("full_fragment", "payloads reconstruct the stream byte-for-byte")
       : devtest::report_fail("full_fragment", "reassembled fragments differ from the stream (FAIL lines above)");
    return ok ? 0 : 1;
}

// lora_full_run_small_transfer_test (tests 13.3 / 13.4)
int cmd_full_small(int argc, char** argv)
{
    return synthetic_transfer("full_small", devtest::arg_u32(argc, argv, 1, 4096), 0);
}

// Test 13.7 deliberate loss (first attempt of every Nth fragment is not transmitted)
int cmd_full_loss(int argc, char** argv)
{
    return synthetic_transfer("full_loss", devtest::arg_u32(argc, argv, 2, 4096), devtest::arg_u32(argc, argv, 1, 5));
}

// lora_full_run_capture_transfer_test (test 13.5 send-once / 13.6 when ACK mode configured)
int cmd_lora_full(int argc, char** argv)
{
    int rc = gated_prepare("lora_full");
    if (rc != 0) return rc;
    data::CaptureSession& s = data::capture_session();
    if (sensors::sensor_manager().run_capture(s, devtest::arg_u32(argc, argv, 1, 30) * 1000) != ESP_OK) {
        devtest::report_fail("lora_full", "SensorManager capture failed");
        return 1;
    }
    comm::CommunicationMetrics m = {};
    comm::CommunicationManager& cm = comm::communication_manager();
    cm.init(comm::CommunicationMode::LORA_FULL);
    data::capture_session_freeze(s);
    const esp_err_t err = cm.send_capture_full(s, m);
    data::capture_session_release(s);
    print_stats(cm.last_lora_full_stats());
    err == ESP_OK ? devtest::report_pass("lora_full", "sender side complete")
                  : devtest::report_fail("lora_full", cownect_err_name(err));
    devtest::report_hw("lora_full", "receiver completeness/loss must be measured on the Heltec tracker");
    return err == ESP_OK ? 0 : 1;
}

// lora_full_run_overrun_test (test 13.8): 20 s cycle, LORA_FULL exceeds it; no cut-off, no sleep.
int cmd_full_overrun(int, char**)
{
    int rc = gated_prepare("full_overrun");
    if (rc != 0) return rc;
    scheduler::SchedulerConfig cfg = scheduler::scheduler_production_config();
    cfg.cycle_target_ms = 20000;
    cfg.capture_ms = 5000;
    // Never deep sleep here: a short transfer + timer wake would re-run this RF test from app_main.
    cfg.allow_deep_sleep = false;
    cfg.communication_enabled = true;
    cfg.communication_mode = comm::CommunicationMode::LORA_FULL;
    scheduler::cycle_scheduler().init(cfg);
    scheduler::cycle_scheduler().run_one_cycle();
    const auto& m = scheduler::cycle_scheduler().last_cycle_metrics();
    print_stats(comm::communication_manager().last_lora_full_stats());
    std::printf("[full_overrun] comm_ms=%u awake_ms=%u overrun_ms=%u sleep_req=%u\n",
                static_cast<unsigned>(m.communication_time_ms), static_cast<unsigned>(m.awake_elapsed_ms),
                static_cast<unsigned>(m.cycle_overrun_ms), static_cast<unsigned>(m.sleep_requested_ms));
    const bool ok = m.cycle_overrun_ms > 0 && m.sleep_requested_ms == 0 && !m.entered_deep_sleep;
    ok ? devtest::report_pass("full_overrun", "transfer not cut at the cycle boundary; overrun recorded")
       : devtest::report_fail("full_overrun", "transfer did not exceed the test cycle or sleep was requested");
    return ok ? 0 : 1;
}

// ---- app_main bring-up entry points (bringup_tests.h) ----------------------------------

void test_serialization()
{
    devtest::begin(13, "M13.1", "CAPTURE STREAM SERIALIZATION (NO HARDWARE)");
    devtest::finish("M13.1", devtest::call(cmd_full_serializer, {"full_serializer"}));
}

void test_crc32()
{
    devtest::begin(13, "M13.2", "CRC-32/ISO-HDLC CHECK VALUE (NO HARDWARE)");
    const bool ok = unit_run_group("crc32");
    if (!ok) devtest::report_fail("crc32", "crc32(\"123456789\") != 0xCBF43926");
    devtest::finish("M13.2", ok ? 0 : 1);
}

void test_fragmentation()
{
    devtest::begin(13, "M13.3", "LORA_FULL FRAGMENTATION (NO HARDWARE)");
    devtest::finish("M13.3", devtest::call(cmd_full_fragment, {"full_fragment"}));
}

void test_packet_codecs()
{
    devtest::begin(13, "M13.4", "PACKET CODECS: TELEMETRY / TEST / ACK / UPLOAD HEADER (NO HARDWARE)");
    const bool ok = unit_run_group("packets");
    if (!ok) devtest::report_fail("packets", "packet encode/decode unit checks failed (FAIL lines above)");
    devtest::finish("M13.4", ok ? 0 : 1);
}

void test_lora_full_small()
{
    devtest::begin(13, "M13.5", "LORA_FULL 4096 B SYNTHETIC STREAM", true);
    devtest::finish("M13.5", devtest::call(cmd_full_small, {"full_small", "4096"}));
}

void test_lora_full()
{
    devtest::begin(13, "M13.6", "LORA_FULL REAL 30 s CAPTURE TRANSFER", true);
    devtest::finish("M13.6", devtest::call(cmd_lora_full, {"lora_full", "30"}));
}

void test_lora_full_loss()
{
    devtest::begin(13, "M13.7", "LORA_FULL DELIBERATE FRAGMENT LOSS", true);
    devtest::finish("M13.7", devtest::call(cmd_full_loss, {"full_loss", "5", "4096"}));
}

void test_lora_full_overrun()
{
    devtest::begin(13, "M13.8", "LORA_FULL BEYOND A 20 s CYCLE", true);
    devtest::finish("M13.8", devtest::call(cmd_full_overrun, {"full_overrun"}));
}
