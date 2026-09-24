#include "nmea_parser.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include "cownect_config.h"

namespace cownect::sensors {
namespace {

constexpr size_t kMaxFields = 24;
constexpr float kKnotsToMps = 0.514444f;

bool is_digit(char c) { return c >= '0' && c <= '9'; }

size_t split_fields(char* text, const char* fields[], size_t max_fields)
{
    size_t n = 0;
    fields[n++] = text;
    for (char* p = text; *p != '\0'; ++p) {
        if (*p == ',') {
            *p = '\0';
            if (n >= max_fields) {
                return 0;  // too many fields -> treat as malformed
            }
            fields[n++] = p + 1;
        }
    }
    return n;
}

bool parse_uint(const char* v, uint32_t max_value, uint32_t& out)
{
    if (v == nullptr || *v == '\0') return false;
    uint32_t x = 0;
    for (const char* p = v; *p != '\0'; ++p) {
        if (!is_digit(*p)) return false;
        x = x * 10 + static_cast<uint32_t>(*p - '0');
        if (x > max_value) return false;
    }
    out = x;
    return true;
}

}  // namespace

bool nmea_parse_decimal(const char* value, float& out)
{
    if (value == nullptr || *value == '\0') return false;
    errno = 0;
    char* end = nullptr;
    const float v = std::strtof(value, &end);
    if (errno != 0 || end == value || *end != '\0') return false;
    out = v;
    return true;
}

bool nmea_parse_coordinate(const char* value, char hemisphere, bool is_latitude, int32_t& out_e7)
{
    if (value == nullptr || *value == '\0') return false;
    const char* p = value;
    int64_t int_part = 0;
    int int_digits = 0;
    while (is_digit(*p)) {
        int_part = int_part * 10 + (*p - '0');
        ++p;
        if (++int_digits > 5) return false;
    }
    if (int_digits < 3) return false;
    int64_t frac_e7 = 0;
    int frac_digits = 0;
    if (*p == '.') {
        ++p;
        while (is_digit(*p)) {
            if (frac_digits < 7) {
                frac_e7 = frac_e7 * 10 + (*p - '0');
                ++frac_digits;
            }
            ++p;
        }
    }
    if (*p != '\0') return false;
    while (frac_digits < 7) {
        frac_e7 *= 10;
        ++frac_digits;
    }
    const int64_t degrees = int_part / 100;
    const int64_t minutes = int_part % 100;
    if (minutes >= 60) return false;
    const int64_t max_deg = is_latitude ? 90 : 180;
    if (degrees > max_deg) return false;
    const int64_t minutes_e7 = minutes * 10000000LL + frac_e7;
    int64_t v = degrees * 10000000LL + (minutes_e7 + 30) / 60;
    if (v > max_deg * 10000000LL) return false;
    if (is_latitude) {
        if (hemisphere == 'S') v = -v;
        else if (hemisphere != 'N') return false;
    } else {
        if (hemisphere == 'W') v = -v;
        else if (hemisphere != 'E') return false;
    }
    out_e7 = static_cast<int32_t>(v);
    return true;
}

bool nmea_parse_time(const char* value, uint8_t& h, uint8_t& m, uint8_t& s, uint16_t& ms)
{
    if (value == nullptr || std::strlen(value) < 6) return false;
    for (int i = 0; i < 6; ++i) {
        if (!is_digit(value[i])) return false;
    }
    const int hh = (value[0] - '0') * 10 + (value[1] - '0');
    const int mm = (value[2] - '0') * 10 + (value[3] - '0');
    const int ss = (value[4] - '0') * 10 + (value[5] - '0');
    if (hh > 23 || mm > 59 || ss > 60) return false;
    int frac = 0;
    int digits = 0;
    const char* p = value + 6;
    if (*p == '.') {
        ++p;
        while (is_digit(*p)) {
            if (digits < 3) {
                frac = frac * 10 + (*p - '0');
                ++digits;
            }
            ++p;
        }
    }
    if (*p != '\0') return false;
    while (digits < 3) {
        frac *= 10;
        ++digits;
    }
    h = static_cast<uint8_t>(hh);
    m = static_cast<uint8_t>(mm);
    s = static_cast<uint8_t>(ss);
    ms = static_cast<uint16_t>(frac);
    return true;
}

bool nmea_parse_date(const char* value, uint8_t& d, uint8_t& mo, uint16_t& y)
{
    if (value == nullptr || std::strlen(value) != 6) return false;
    for (int i = 0; i < 6; ++i) {
        if (!is_digit(value[i])) return false;
    }
    const int dd = (value[0] - '0') * 10 + (value[1] - '0');
    const int mm = (value[2] - '0') * 10 + (value[3] - '0');
    const int yy = (value[4] - '0') * 10 + (value[5] - '0');
    if (dd < 1 || dd > 31 || mm < 1 || mm > 12) return false;
    d = static_cast<uint8_t>(dd);
    mo = static_cast<uint8_t>(mm);
    y = static_cast<uint16_t>(2000 + yy);  // NMEA RMC carries a 2-digit year
    return true;
}

esp_err_t StandardNmeaParser::consume_sentence(const char* sentence, size_t length, GpsFixUpdate* update)
{
    if (sentence == nullptr || update == nullptr || length == 0 || length > config::GPS_MAX_SENTENCE_BYTES) {
        return ESP_ERR_INVALID_ARG;
    }
    char text[config::GPS_MAX_SENTENCE_BYTES + 1];
    std::memcpy(text, sentence, length);
    text[length] = '\0';
    const char* f[kMaxFields];
    const size_t n = split_fields(text, f, kMaxFields);
    if (n == 0) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    const size_t id_len = std::strlen(f[0]);
    if (id_len < 5 || f[0][0] == 'P') {
        return ESP_ERR_NOT_SUPPORTED;  // proprietary or unknown
    }
    const char* type = f[0] + id_len - 3;

    GpsFixUpdate u = {};
    if (std::strcmp(type, "RMC") == 0) {
        if (n < 10) return ESP_ERR_INVALID_RESPONSE;
        u.kind = GpsFixUpdate::Kind::RMC;
        if (f[2][0] != '\0') {
            if (f[2][1] != '\0' || (f[2][0] != 'A' && f[2][0] != 'V')) return ESP_ERR_INVALID_RESPONSE;
            u.has_rmc_status = true;
            u.rmc_status_active = f[2][0] == 'A';
        }
        if (f[3][0] != '\0' || f[5][0] != '\0') {
            int32_t lat = 0, lon = 0;
            if (!nmea_parse_coordinate(f[3], f[4][0], true, lat) || !nmea_parse_coordinate(f[5], f[6][0], false, lon)) {
                return ESP_ERR_INVALID_RESPONSE;
            }
            u.has_position = true;
            u.latitude_e7 = lat;
            u.longitude_e7 = lon;
        }
        float knots = 0.0f;
        if (f[7][0] != '\0') {
            if (!nmea_parse_decimal(f[7], knots)) return ESP_ERR_INVALID_RESPONSE;
            u.has_speed = true;
            u.speed_mps = knots * kKnotsToMps;
        }
        if (f[8][0] != '\0') {
            if (!nmea_parse_decimal(f[8], u.course_deg)) return ESP_ERR_INVALID_RESPONSE;
            u.has_course = true;
        }
        if (f[9][0] != '\0') {
            if (!nmea_parse_date(f[9], u.day, u.month, u.year)) return ESP_ERR_INVALID_RESPONSE;
            u.has_date = true;
        }
    } else if (std::strcmp(type, "GGA") == 0) {
        if (n < 8) return ESP_ERR_INVALID_RESPONSE;
        u.kind = GpsFixUpdate::Kind::GGA;
        if (f[2][0] != '\0' || f[4][0] != '\0') {
            int32_t lat = 0, lon = 0;
            if (!nmea_parse_coordinate(f[2], f[3][0], true, lat) || !nmea_parse_coordinate(f[4], f[5][0], false, lon)) {
                return ESP_ERR_INVALID_RESPONSE;
            }
            u.has_position = true;
            u.latitude_e7 = lat;
            u.longitude_e7 = lon;
        }
        uint32_t q = 0, sats = 0;
        if (f[6][0] != '\0') {
            if (!parse_uint(f[6], 9, q)) return ESP_ERR_INVALID_RESPONSE;
            u.has_gga_quality = true;
            u.gga_quality = static_cast<uint8_t>(q);
        }
        if (f[7][0] != '\0') {
            if (!parse_uint(f[7], 99, sats)) return ESP_ERR_INVALID_RESPONSE;
            u.has_satellites = true;
            u.satellites = static_cast<uint8_t>(sats);
        }
    } else {
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (f[1][0] != '\0') {
        if (!nmea_parse_time(f[1], u.hour, u.minute, u.second, u.millisecond)) {
            return ESP_ERR_INVALID_RESPONSE;
        }
        u.has_time = true;
        u.time_key = ((u.hour * 100u + u.minute) * 100u + u.second) * 1000u + u.millisecond;
    }
    *update = u;
    return ESP_OK;
}

}  // namespace cownect::sensors
