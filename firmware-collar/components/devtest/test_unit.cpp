// Pure-function unit tests (Materials 5, 6, 7, 11, 13, 14, 15). No PCB needed.
// These exercise the production helpers directly; fixtures are small synthetic values, never
// presented as real module output.
#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "accelerometer_decode.h"
#include "byte_codec.h"
#include "capture_stream_decoder.h"
#include "capture_stream_encoder.h"
#include "capture_stream_format.h"
#include "crc32.h"
#include "devtest_common.h"
#include "feature_processor.h"
#include "gps_fix_builder.h"
#include "lora_full_protocol.h"
#include "lora_full_transfer.h"
#include "lora_test_packet.h"
#include "nmea_framer.h"
#include "nmea_parser.h"
#include "prototype_telemetry_encoder.h"
#include "synthetic_stream.h"
#include "temperature_math.h"
#include "wifi_capture_uploader.h"

using namespace cownect;

namespace {

int g_fail = 0;
#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (!(cond)) {                                                                \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
            ++g_fail;                                                                 \
        }                                                                             \
    } while (0)

bool near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

// ---- small deterministic CaptureSession fixture -----------------------------------------
data::AccelSample f_accel[3] = {{100, -200, 4096}, {-1, 0, 32767}, {-32768, 5, -7}};
data::GpsFix f_gps[2];
data::BoardTemperatureSample f_board[2];
data::CowTemperatureSample f_cow[2];
uint16_t f_mic[5] = {0, 1, 2048, 4095, 1234};
data::CaptureSession f_session;

void build_fixture()
{
    f_gps[0] = {};
    f_gps[0].capture_offset_ms = 1000;
    f_gps[0].latitude_e7 = 340520567;
    f_gps[0].longitude_e7 = -1171234567;
    f_gps[0].speed_mps = 1.5f;
    f_gps[0].course_deg = 270.25f;
    f_gps[0].fix_quality = 1;
    f_gps[0].satellites = 9;
    f_gps[0].valid = true;
    f_gps[0].utc_valid = true;
    f_gps[1] = f_gps[0];
    f_gps[1].capture_offset_ms = 2000;
    f_gps[1].latitude_e7 = 340520600;
    f_board[0] = {1000, 1100, 750, true, 25.0f, sensors::TemperatureStatus::OK};
    f_board[1] = {2000, 1105, 0, false, NAN, sensors::TemperatureStatus::ADC_CALIBRATION_UNAVAILABLE};
    f_cow[0] = {1000, 1500, 1210, true, 5758.0f, 38.0f, sensors::TemperatureStatus::OK};
    f_cow[1] = {2000, 4095, 3100, true, NAN, NAN, sensors::TemperatureStatus::SENSOR_OPEN_SUSPECTED};
    f_session = {};
    f_session.id = {0x0000000000000001ULL, 42};
    f_session.capture_start_us = 123456789ULL;
    f_session.capture_end_us = 153456789ULL;
    f_session.accel_samples = f_accel;
    f_session.accel_count = 3;
    f_session.gps_fixes = f_gps;
    f_session.gps_fix_count = 2;
    f_session.board_temperature_samples = f_board;
    f_session.board_temperature_count = 2;
    f_session.cow_temperature_samples = f_cow;
    f_session.cow_temperature_count = 2;
    f_session.microphone_raw_samples = f_mic;
    f_session.microphone_sample_count = 5;
    f_session.microphone_sample_rate_hz = 32000;
    f_session.status.accel_ok = true;
    f_session.status.gps_ok = true;
    f_session.status.gps_had_valid_fix = true;
    f_session.status.cow_probe_fault = true;
    f_session.diagnostics.requested_capture_us = 30000000;
    f_session.diagnostics.actual_capture_us = 30000000;
    f_session.state = data::CaptureState::COMPLETE;
}

bool same_float(float a, float b) { return (std::isnan(a) && std::isnan(b)) || a == b; }

// ---- groups --------------------------------------------------------------------------------

void unit_accel_decode()
{
    const uint8_t b[6] = {0x34, 0x12, 0xFF, 0xFF, 0x00, 0x80};
    const auto s = sensors::decode_xyz(b);
    CHECK(s.x_raw == 0x1234 && s.y_raw == -1 && s.z_raw == -32768);
    const auto f = sensors::decode_fifo_status(0xC5);
    CHECK(f.threshold_reached && f.overrun && f.unread == 5);
    // +1 g at +/-4 g HP: 1000 mg / 0.488 = 2049 digits -> word = 2049 << 2
    CHECK(near(sensors::accel_raw_to_g(static_cast<int16_t>(2049 << 2)), 1.0f, 0.001f));
    CHECK(near(sensors::accel_raw_to_g(static_cast<int16_t>(-(2049 << 2))), -1.0f, 0.001f));
}

void unit_temperature_math()
{
    CHECK(near(sensors::mcp9700_mv_to_celsius(500), 0.0f, 1e-4f));
    CHECK(near(sensors::mcp9700_mv_to_celsius(750), 25.0f, 1e-4f));
    CHECK(near(sensors::mcp9700_mv_to_celsius(900), 40.0f, 1e-4f));
    float r = 0;
    CHECK(sensors::ntc_divider_resistance(1650, 3300, 10000, r) && near(r, 10000.0f, 1.0f));
    CHECK(!sensors::ntc_divider_resistance(0, 3300, 10000, r));
    CHECK(!sensors::ntc_divider_resistance(3300, 3300, 10000, r));
    CHECK(!sensors::ntc_divider_resistance(3400, 3300, 10000, r));
    CHECK(!sensors::ntc_divider_resistance(NAN, 3300, 10000, r));
    float c = 0;
    CHECK(sensors::ntc_beta_resistance_to_celsius(10000, 10000, 3950, c) && near(c, 25.0f, 0.01f));
    CHECK(sensors::ntc_beta_resistance_to_celsius(5758, 10000, 3950, c) && near(c, 38.0f, 0.6f));
    CHECK(!sensors::ntc_beta_resistance_to_celsius(0, 10000, 3950, c));
    CHECK(!sensors::ntc_beta_resistance_to_celsius(-5, 10000, 3950, c));
    CHECK(!sensors::ntc_beta_resistance_to_celsius(INFINITY, 10000, 3950, c));
}

void unit_nmea()
{
    using sensors::NmeaFramer;
    // Standard NMEA example sentence (checksum computed by the framer helper, not module output).
    const char* body = "GPRMC,123519.00,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W";
    char line[160];
    std::snprintf(line, sizeof(line), "$%s*%02X\r\n", body, NmeaFramer::checksum(body, std::strlen(body)));
    NmeaFramer fr;
    NmeaFramer::Result res = NmeaFramer::Result::NONE;
    for (const char* p = line; *p; ++p) {
        const auto r = fr.push(*p);
        if (r != NmeaFramer::Result::NONE) res = r;
    }
    CHECK(res == NmeaFramer::Result::SENTENCE && fr.checksum_present());
    sensors::StandardNmeaParser parser;
    sensors::GpsFixUpdate u = {};
    CHECK(parser.consume_sentence(fr.body(), fr.body_length(), &u) == ESP_OK);
    CHECK(u.kind == sensors::GpsFixUpdate::Kind::RMC && u.has_position && u.rmc_status_active);
    CHECK(u.latitude_e7 == 481173000 && u.longitude_e7 == 115166667);
    CHECK(u.has_time && u.hour == 12 && u.minute == 35 && u.second == 19);
    CHECK(u.has_date && u.day == 23 && u.month == 3);
    // Bad checksum is rejected
    fr.reset();
    std::snprintf(line, sizeof(line), "$%s*00\r\n", body);
    res = NmeaFramer::Result::NONE;
    for (const char* p = line; *p; ++p) {
        const auto r = fr.push(*p);
        if (r != NmeaFramer::Result::NONE) res = r;
    }
    CHECK(res == NmeaFramer::Result::CHECKSUM_ERROR);
    // Overlength input resets without growing memory
    fr.reset();
    fr.push('$');
    bool overlength = false;
    for (int i = 0; i < 400; ++i) overlength |= fr.push('A') == NmeaFramer::Result::OVERLENGTH;
    CHECK(overlength);
    // Coordinates, hemispheres
    int32_t v = 0;
    CHECK(sensors::nmea_parse_coordinate("3403.123402", 'N', true, v) && v == 340520567);
    CHECK(sensors::nmea_parse_coordinate("11707.407402", 'W', false, v) && v == -1171234567);
    CHECK(!sensors::nmea_parse_coordinate("4860.000", 'N', true, v));
    CHECK(!sensors::nmea_parse_coordinate("4807.038", 'E', true, v));
    // No-fix RMC: status V -> fix invalid
    char nofix[] = "GNRMC,000001.00,V,,,,,,,010125,,,N";
    CHECK(parser.consume_sentence(nofix, std::strlen(nofix), &u) == ESP_OK && !u.has_position && !u.rmc_status_active);
    // Builder: same-epoch GGA+RMC merge; emission on next epoch; duplicate epoch suppressed
    sensors::GpsFixBuilder b;
    b.reset();
    char gga[] = "GPGGA,123519.00,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,";
    char rmc[] = "GPRMC,123519.00,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W";
    char gga2[] = "GPGGA,123520.00,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,";
    sensors::GpsFix out = {};
    bool dup = false;
    CHECK(parser.consume_sentence(gga, std::strlen(gga), &u) == ESP_OK);
    CHECK(b.feed(u, 0, out, dup) == sensors::GpsFixBuilder::FeedResult::MERGED);
    CHECK(parser.consume_sentence(rmc, std::strlen(rmc), &u) == ESP_OK);
    CHECK(b.feed(u, 5, out, dup) == sensors::GpsFixBuilder::FeedResult::MERGED);
    CHECK(parser.consume_sentence(gga2, std::strlen(gga2), &u) == ESP_OK);
    CHECK(b.feed(u, 1000, out, dup) == sensors::GpsFixBuilder::FeedResult::EMITTED);
    CHECK(out.valid && !dup && out.fix_quality == 1 && out.satellites == 8 && out.latitude_e7 == 481173000);
    // Duplicate epoch: epoch 123519 emitted, then the same epoch arrives again -> flagged.
    b.reset();
    CHECK(parser.consume_sentence(gga, std::strlen(gga), &u) == ESP_OK);
    b.feed(u, 0, out, dup);
    CHECK(b.flush(out, dup) && !dup && out.valid);
    b.feed(u, 10, out, dup);
    CHECK(b.flush(out, dup) && dup);
}

void unit_crc()
{
    const uint8_t s[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    CHECK(codec::crc32(s, sizeof(s)) == 0xCBF43926u);
}

size_t read_all(const codec::IByteStream& st, uint8_t* dst, size_t cap)
{
    size_t total = 0;
    while (total < st.total_bytes()) {
        size_t n = 0;
        if (st.read(static_cast<uint32_t>(total), dst + total, cap - total, n) != ESP_OK || n == 0) break;
        total += n;
    }
    return total;
}

}  // namespace

bool unit_capture_stream_roundtrip()
{
    const int before = g_fail;
    build_fixture();
    codec::CaptureStreamEncoder enc;
    CHECK(enc.prepare(f_session) == ESP_OK);
    const uint32_t total = enc.total_bytes();
    const uint32_t expected = 68 + 6 * 12 + 3 * 6 + 2 * 24 + 2 * 20 + 2 * 24 + 5 * 2 + 144;
    CHECK(total == expected);
    static uint8_t a[1024];
    static uint8_t b[1024];
    CHECK(total <= sizeof(a));
    CHECK(read_all(enc, a, sizeof(a)) == total);
    CHECK(read_all(enc, b, sizeof(b)) == total);
    CHECK(std::memcmp(a, b, total) == 0);  // deterministic
    // Random-offset reads across section boundaries equal the full stream.
    srand(1234);
    for (int i = 0; i < 200; ++i) {
        const uint32_t off = static_cast<uint32_t>(rand()) % total;
        const size_t len = 1 + static_cast<size_t>(rand()) % 37;
        uint8_t tmp[40];
        size_t n = 0;
        CHECK(enc.read(off, tmp, len, n) == ESP_OK);
        CHECK(n == std::min<size_t>(len, total - off) && std::memcmp(tmp, a + off, n) == 0);
    }
    size_t n = 0;
    uint8_t tmp[4];
    CHECK(enc.read(total, tmp, sizeof(tmp), n) == ESP_OK && n == 0);  // last-byte boundary
    CHECK(enc.read(total + 1, tmp, sizeof(tmp), n) != ESP_OK);          // out of range rejected
    // Decode and compare every retained value.
    codec::ParsedCaptureStream p;
    CHECK(codec::capture_stream_parse(a, total, p) == ESP_OK);
    CHECK(p.device_id == 1 && p.capture_id == 42 && p.total_stream_bytes == total);
    CHECK(p.accel_count == 3 && p.gps_count == 2 && p.board_temp_count == 2 && p.cow_temp_count == 2 &&
          p.microphone_count == 5 && p.microphone_sample_rate_hz == 32000);
    CHECK(p.capture_start_us == 123456789ULL && p.capture_end_us == 153456789ULL);
    const codec::ParsedSection* sa = codec::capture_stream_find(p, codec::stream::SECTION_ACCEL_RAW);
    const codec::ParsedSection* sg = codec::capture_stream_find(p, codec::stream::SECTION_GPS_FIXES);
    const codec::ParsedSection* sb = codec::capture_stream_find(p, codec::stream::SECTION_BOARD_TEMP);
    const codec::ParsedSection* sc = codec::capture_stream_find(p, codec::stream::SECTION_COW_TEMP);
    const codec::ParsedSection* sm = codec::capture_stream_find(p, codec::stream::SECTION_MICROPHONE_RAW);
    const codec::ParsedSection* sd = codec::capture_stream_find(p, codec::stream::SECTION_DIAGNOSTICS);
    CHECK(sa && sg && sb && sc && sm && sd);
    if (sa && sg && sb && sc && sm && sd) {
        for (int i = 0; i < 3; ++i) {
            const auto s = codec::decode_accel_record(a + sa->payload_offset + i * 6);
            CHECK(s.x_raw == f_accel[i].x_raw && s.y_raw == f_accel[i].y_raw && s.z_raw == f_accel[i].z_raw);
        }
        for (int i = 0; i < 2; ++i) {
            const auto g = codec::decode_gps_record(a + sg->payload_offset + i * codec::stream::GPS_RECORD_BYTES);
            CHECK(g.capture_offset_ms == f_gps[i].capture_offset_ms && g.latitude_e7 == f_gps[i].latitude_e7 &&
                  g.longitude_e7 == f_gps[i].longitude_e7 && g.speed_mps == f_gps[i].speed_mps &&
                  g.course_deg == f_gps[i].course_deg && g.valid && g.utc_valid && g.satellites == 9);
            const auto bt = codec::decode_board_temp_record(a + sb->payload_offset + i * 20);
            CHECK(bt.timestamp_ms == f_board[i].timestamp_ms && bt.adc_raw == f_board[i].adc_raw &&
                  bt.millivolts_valid == f_board[i].millivolts_valid && bt.status == f_board[i].status &&
                  same_float(bt.temperature_c, f_board[i].temperature_c));
            const auto ct = codec::decode_cow_temp_record(a + sc->payload_offset + i * 24);
            CHECK(ct.adc_raw == f_cow[i].adc_raw && ct.status == f_cow[i].status &&
                  same_float(ct.resistance_ohm, f_cow[i].resistance_ohm) &&
                  same_float(ct.temperature_c, f_cow[i].temperature_c));
        }
        for (int i = 0; i < 5; ++i) {
            codec::ByteReader r(a + sm->payload_offset + i * 2, 2);
            CHECK(r.u16() == f_mic[i]);
        }
        CHECK(sd->payload_bytes == codec::stream::DIAGNOSTICS_RECORD_BYTES);
    }
    return g_fail == before;
}

bool unit_fragment_reconstruction(uint16_t payload_bytes)
{
    const int before = g_fail;
    build_fixture();
    codec::CaptureStreamEncoder enc;
    CHECK(enc.prepare(f_session) == ESP_OK);
    static uint8_t full[1024];
    static uint8_t rebuilt[1024];
    const size_t total = read_all(enc, full, sizeof(full));
    lorafull::LoraFullConfig cfg = {};
    cfg.protocol_version = lorafull::LORA_FULL_PROTOCOL_VERSION;
    cfg.fragment_payload_bytes = payload_bytes;
    lorafull::CaptureFragmenter fr;
    const bool fits = lorafull::DATA_HEADER_BYTES + payload_bytes <= 255;
    const esp_err_t err = fr.begin(enc, cfg, 1, 42, false, 0);
    if (!fits) {
        CHECK(err == ESP_ERR_INVALID_SIZE);  // oversized configuration rejected
        return g_fail == before;
    }
    CHECK(err == ESP_OK);
    CHECK(fr.fragment_count() == (total + payload_bytes - 1) / payload_bytes);
    size_t pos = 0;
    for (uint32_t i = 0; i < fr.fragment_count(); ++i) {
        uint8_t pkt[255];
        size_t len = 0;
        CHECK(fr.build_fragment(i, pkt, sizeof(pkt), len) == ESP_OK);
        lorafull::DataHeader h = {};
        CHECK(lorafull::decode_data_header(pkt, len, h) && lorafull::data_header_bounds_ok(h, len));
        CHECK(h.fragment_index == i && h.stream_offset == pos && h.capture_id == 42 && h.total_stream_bytes == total);
        CHECK(((h.flags & lorafull::FLAG_LAST_FRAGMENT) != 0) == (i + 1 == fr.fragment_count()));
        std::memcpy(rebuilt + pos, pkt + lorafull::DATA_HEADER_BYTES, h.payload_bytes);
        pos += h.payload_bytes;
    }
    CHECK(pos == total && std::memcmp(full, rebuilt, total) == 0);
    return g_fail == before;
}

namespace {

void unit_fragmenter()
{
    for (uint16_t p : {1, 7, 64, 150, 215, 216}) {
        CHECK(unit_fragment_reconstruction(p));
    }
    // Synthetic stream across several fragments
    codec::SyntheticStream st(3000, 0x11);
    uint8_t buf[16];
    size_t n = 0;
    CHECK(st.read(2990, buf, sizeof(buf), n) == ESP_OK && n == 10 && buf[0] == codec::SyntheticStream::byte_at(2990, 0x11));
}

void unit_packets()
{
    // Telemetry encode/decode
    features::TelemetryRecord t = {};
    t.device_id = 1;
    t.capture_id = 42;
    t.gps_valid = true;
    t.latitude_e7 = 1;
    t.longitude_e7 = -2;
    t.cow_temp_valid = false;
    t.cow_temp_c = 99.0f;  // must NOT be transmitted as valid
    t.accel_valid = true;
    t.accel_magnitude_mean_g = 1.0f;
    t.status_flags = 0x21;
    uint8_t buf[64];
    size_t n = 0;
    CHECK(hybrid::PrototypeTelemetryEncoder().encode(t, buf, sizeof(buf), n) == ESP_OK && n == 49);
    features::TelemetryRecord d = {};
    CHECK(hybrid::prototype_telemetry_decode(buf, n, d) == ESP_OK);
    CHECK(d.device_id == 1 && d.capture_id == 42 && d.gps_valid && d.latitude_e7 == 1 && d.longitude_e7 == -2);
    CHECK(!d.cow_temp_valid && d.cow_temp_c == 0.0f && d.accel_valid && d.status_flags == 0x21);
    CHECK(hybrid::prototype_telemetry_decode(buf, n - 1, d) != ESP_OK);
    // LoRa test packet
    radio::LoraTestPacket p = {radio::LORA_TEST_VERSION, radio::LoraTestType::PING, 0x1122334455667788ULL, 7, 9};
    n = radio::lora_test_packet_encode(p, buf, sizeof(buf));
    radio::LoraTestPacket q = {};
    CHECK(n == 20 && buf[0] == 0x43 && buf[1] == 0x54 && radio::lora_test_packet_decode(buf, n, q));
    CHECK(q.device_id == p.device_id && q.sequence == 7 && q.value == 9 && q.type == radio::LoraTestType::PING);
    // LORA_FULL ACK
    lorafull::Ack a = {1, 42, 17, 0};
    n = lorafull::encode_ack(a, buf, sizeof(buf));
    lorafull::Ack b = {};
    CHECK(n == lorafull::ACK_BYTES && lorafull::decode_ack(buf, n, b) && b.fragment_index == 17 && b.capture_id == 42);
    // Upload header
    n = upload::encode_upload_header(1, 42, 1932456, buf, sizeof(buf));
    CHECK(n == upload::UPLOAD_HEADER_BYTES && buf[0] == 'C' && buf[1] == 'N' && buf[2] == 'U' && buf[3] == 'P');
    codec::ByteReader r(buf, n);
    r.u32();
    CHECK(r.u8() == 1 && r.u64() == 1 && r.u32() == 42 && r.u32() == 1932456);
}

void unit_features()
{
    build_fixture();
    CHECK(features::latest_valid_gps(f_gps, 2) == 1);
    CHECK(features::latest_valid_board_temp(f_board, 2) == 0);
    CHECK(features::latest_valid_cow_temp(f_cow, 2) == 0);
    const data::AccelSample flat[2] = {{0, 0, static_cast<int16_t>(2049 << 2)}, {0, 0, static_cast<int16_t>(2049 << 2)}};
    const auto m = features::summarize_accel_magnitude(flat, 2);
    CHECK(m.valid && near(m.mean_g, 1.0f, 0.001f) && near(m.rms_g, 1.0f, 0.001f));
    CHECK(!features::summarize_accel_magnitude(nullptr, 0).valid);
    features::TelemetryRecord t = {};
    CHECK(features::SimpleFeatureProcessor().process(f_session, t) == ESP_OK);
    CHECK(t.capture_id == 42 && t.gps_valid && t.latitude_e7 == 340520600);
    CHECK(t.cow_temp_valid && near(t.cow_temp_c, 38.0f, 1e-4f));
    CHECK(!t.microphone_valid);  // fixture has no microphone_ok -> explicitly invalid
    CHECK((t.status_flags & features::TELEMETRY_STATUS_COW_PROBE_FAULT) != 0);
    CHECK((t.status_flags & features::TELEMETRY_STATUS_MIC_ERROR) != 0);
}

struct Group {
    const char* name;
    void (*fn)();
};

const Group kGroups[] = {
    {"accel_decode", unit_accel_decode},
    {"temperature_math", unit_temperature_math},
    {"nmea", unit_nmea},
    {"crc32", unit_crc},
    {"capture_stream", [] { CHECK(unit_capture_stream_roundtrip()); }},
    {"fragmenter", unit_fragmenter},
    {"packets", unit_packets},
    {"features", unit_features},
};

bool run_group(const Group& g)
{
    const int before = g_fail;
    g.fn();
    std::printf("[unit] %-18s %s\n", g.name, g_fail == before ? "PASS" : "FAIL");
    return g_fail == before;
}

}  // namespace

bool unit_run_group(const char* name)
{
    for (const auto& g : kGroups) {
        if (std::strcmp(g.name, name) == 0) {
            return run_group(g);
        }
    }
    std::printf("[unit] unknown group '%s'\n", name);
    return false;
}

int cmd_unit(int, char**)
{
    g_fail = 0;
    for (const auto& g : kGroups) {
        run_group(g);
    }
    g_fail == 0 ? devtest::report_pass("unit", "all pure-function tests passed")
                : devtest::report_fail("unit", "pure-function checks failed (see FAIL lines)");
    return g_fail == 0 ? 0 : 1;
}

// ---- app_main bring-up entry point -------------------------------------------------------
// Declared in bringup_tests.h (not included here: this file keeps its own unit_* names).
void test_unit_all()
{
    devtest::begin(0, "U.1", "ALL PURE-FUNCTION UNIT TESTS (NO PCB PERIPHERALS)");
    devtest::finish("U.1", cmd_unit(0, nullptr));
}
