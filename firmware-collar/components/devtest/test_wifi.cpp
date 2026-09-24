// Material 14 Rev B: Wi-Fi / TCP tests (real WifiManager, PrototypeTcpClient, uploader).
#include <cstdio>
#include "capture_session.h"
#include "cownect_err.h"
#include "board_identity.h"
#include "bringup_tests.h"
#include "devtest_common.h"
#include "prototype_tcp_client.h"
#include "prototype_wifi_config.h"
#include "sensor_manager.h"
#include "synthetic_stream.h"
#include "wifi_capture_uploader.h"
#include "wifi_manager.h"

using namespace cownect;

namespace {

void print_upload(const upload::WifiUploadStats& s)
{
    std::printf("[UPLOAD] capture=%u stream=%u sent=%u wifi_ms=%u tcp_ms=%u upload_ms=%u writes=%u sock_err=%u "
                "wifi=%d tcp=%d complete=%d stage=%s\n",
                static_cast<unsigned>(s.capture_id), static_cast<unsigned>(s.stream_bytes),
                static_cast<unsigned>(s.bytes_sent), static_cast<unsigned>(s.wifi_connect_ms),
                static_cast<unsigned>(s.tcp_connect_ms), static_cast<unsigned>(s.upload_ms),
                static_cast<unsigned>(s.socket_write_calls), static_cast<unsigned>(s.socket_errors), s.wifi_connected,
                s.tcp_connected, s.upload_complete, s.failure_stage);
}

}  // namespace

// wifi_run_connect_test (test 14.1)
int cmd_wifi_connect(int, char**)
{
    wifi::PrototypeWifiConfig cfg;
    const char* missing = nullptr;
    esp_err_t err = wifi::prototype_wifi_config_load_wifi_only(cfg, &missing);
    if (err != ESP_OK) {
        devtest::report_blocked("wifi_connect", err, missing);
        return 2;
    }
    wifi::WifiManager& wm = wifi::wifi_manager();
    err = wm.connect(cfg);
    std::printf("[WIFI] connect=%s connect_ms=%u ip=%s\n", cownect_err_name(err),
                static_cast<unsigned>(wm.last_connect_ms()), wm.ip_string());
    wm.disconnect();
    wm.stop();
    char why[96];
    std::snprintf(why, sizeof(why), "no IP within the configured connect timeout (%s)", cownect_err_name(err));
    err == ESP_OK ? devtest::report_pass("wifi_connect", "IP obtained, clean stop")
                  : devtest::report_fail("wifi_connect", why);
    return err == ESP_OK ? 0 : 1;
}

// wifi_run_tcp_test (test 14.2): short text payload, no upload header.
int cmd_wifi_tcp(int, char**)
{
    wifi::PrototypeWifiConfig cfg;
    const char* missing = nullptr;
    esp_err_t err = wifi::prototype_wifi_config_load(cfg, &missing);
    if (err != ESP_OK) {
        devtest::report_blocked("wifi_tcp", err, missing);
        return 2;
    }
    wifi::WifiManager& wm = wifi::wifi_manager();
    tcp::PrototypeTcpClient c;
    err = wm.connect(cfg);
    if (err == ESP_OK) err = c.connect(cfg.jetson_ip, cfg.jetson_port, cfg.tcp_connect_timeout_ms);
    static const char kMsg[] = "COWNECT TCP TEST\n";
    if (err == ESP_OK) {
        err = c.send_all(reinterpret_cast<const uint8_t*>(kMsg), sizeof(kMsg) - 1, cfg.socket_write_timeout_ms);
    }
    std::printf("[TCP] result=%s bytes_sent=%llu\n", cownect_err_name(err), static_cast<unsigned long long>(c.bytes_sent()));
    c.close();
    const bool wifi_up = wm.connected();
    wm.disconnect();
    wm.stop();
    char why[112];
    std::snprintf(why, sizeof(why), "%s failed (%s)", wifi_up ? "TCP connect/send to the Jetson" : "Wi-Fi connect",
                  cownect_err_name(err));
    err == ESP_OK ? devtest::report_pass("wifi_tcp") : devtest::report_fail("wifi_tcp", why);
    devtest::report_hw("wifi_tcp", "Jetson must print exactly the 17-byte test string");
    return err == ESP_OK ? 0 : 1;
}

// wifi_run_small_upload_test (test 14.3): deterministic generated stream with the upload header.
int cmd_wifi_small_upload(int argc, char** argv)
{
    const uint32_t bytes = devtest::arg_u32(argc, argv, 1, 65536);
    codec::SyntheticStream stream(bytes, 0xA5);
    upload::WifiUploadStats s;
    const esp_err_t err = upload::wifi_uploader().upload_stream(stream, board_device_id(), 0xFFFF0000u, s);
    print_upload(s);
    if (err == COWNECT_ERR_NOT_CONFIGURED) {
        devtest::report_blocked("wifi_small_upload", err, "Wi-Fi/Jetson values");
        return 2;
    }
    char why[96];
    std::snprintf(why, sizeof(why), "upload failed at stage %s (%s)", s.failure_stage ? s.failure_stage : "?",
                  cownect_err_name(err));
    err == ESP_OK ? devtest::report_pass("wifi_small_upload") : devtest::report_fail("wifi_small_upload", why);
    devtest::report_hw("wifi_small_upload", "Jetson verifies length and content: byte(i)=(i*31+(i>>8)+0xA5)&0xFF");
    return err == ESP_OK ? 0 : 1;
}

// wifi_run_capture_upload_test (test 14.4)
int cmd_wifi_capture_upload(int argc, char** argv)
{
    if (!devtest::ensure_idle("wifi_capture_upload")) return 1;
    wifi::PrototypeWifiConfig cfg;
    const char* missing = nullptr;
    if (wifi::prototype_wifi_config_load(cfg, &missing) != ESP_OK) {
        devtest::report_blocked("wifi_capture_upload", COWNECT_ERR_NOT_CONFIGURED, missing);
        return 2;
    }
    data::CaptureSession& s = data::capture_session();
    // Wi-Fi stays off during the capture (Material 14 section 20).
    if (sensors::sensor_manager().run_capture(s, devtest::arg_u32(argc, argv, 1, 30) * 1000) != ESP_OK) {
        devtest::report_fail("wifi_capture_upload", "SensorManager capture failed");
        return 1;
    }
    devtest::rail_off();  // no switched-rail user needed for Wi-Fi
    upload::WifiUploadStats st;
    data::capture_session_freeze(s);
    const esp_err_t err = upload::wifi_uploader().upload_capture(s, st);
    data::capture_session_release(s);
    print_upload(st);
    char why[96];
    std::snprintf(why, sizeof(why), "upload failed at stage %s (%s)", st.failure_stage ? st.failure_stage : "?",
                  cownect_err_name(err));
    err == ESP_OK ? devtest::report_pass("wifi_capture_upload", "bytes_sent == stream_bytes")
                  : devtest::report_fail("wifi_capture_upload", why);
    devtest::report_hw("wifi_capture_upload", "Jetson: same device_id/capture_id and received_bytes == stream_bytes");
    return err == ESP_OK ? 0 : 1;
}

// wifi_run_repeat_test (test 14.8)
int cmd_wifi_repeat(int argc, char** argv)
{
    const uint32_t count = devtest::arg_u32(argc, argv, 1, 3);
    int rc = 0;
    for (uint32_t i = 0; i < count; ++i) {
        char arg0[] = "wifi_capture_upload";
        char arg1[] = "5";
        char* av[] = {arg0, arg1};
        const int r = cmd_wifi_capture_upload(2, av);
        if (r == 2) return 2;  // CONFIG_NOT_SET: no point repeating
        rc |= r;
    }
    return rc;
}

// ---- app_main bring-up entry points (bringup_tests.h) ----------------------------------

void test_wifi_connect()
{
    devtest::begin(14, "M14.1", "WI-FI CONNECT");
    devtest::finish("M14.1", devtest::call(cmd_wifi_connect, {"wifi_connect"}));
}

void test_wifi_tcp()
{
    devtest::begin(14, "M14.2", "WI-FI TCP MESSAGE TO JETSON");
    devtest::finish("M14.2", devtest::call(cmd_wifi_tcp, {"wifi_tcp"}));
}

void test_wifi_small_upload()
{
    devtest::begin(14, "M14.3", "WI-FI 64 KiB GENERATED STREAM UPLOAD");
    devtest::finish("M14.3", devtest::call(cmd_wifi_small_upload, {"wifi_small_upload", "65536"}));
}

void test_wifi_capture_upload()
{
    devtest::begin(14, "M14.4", "WI-FI REAL 30 s CAPTURE UPLOAD");
    devtest::finish("M14.4", devtest::call(cmd_wifi_capture_upload, {"wifi_capture_upload", "30"}));
}

void test_wifi_repeat()
{
    devtest::begin(14, "M14.5", "WI-FI 3 x 5 s CAPTURE UPLOADS");
    devtest::finish("M14.5", devtest::call(cmd_wifi_repeat, {"wifi_repeat", "3"}));
}
