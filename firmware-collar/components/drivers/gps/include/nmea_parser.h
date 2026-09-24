#pragma once

#include <cstddef>
#include <cstdint>
#include "esp_err.h"

namespace cownect::sensors {

// Partial navigation update parsed from one standard NMEA sentence.
struct GpsFixUpdate {
    enum class Kind { RMC, GGA } kind;

    bool has_time;
    uint32_t time_key;  // hhmmss * 1000 + ms: identifies the navigation epoch
    uint8_t hour, minute, second;
    uint16_t millisecond;

    bool has_date;
    uint8_t day, month;
    uint16_t year;

    bool has_position;
    int32_t latitude_e7;
    int32_t longitude_e7;

    bool has_rmc_status;
    bool rmc_status_active;  // 'A'

    bool has_speed;
    float speed_mps;
    bool has_course;
    float course_deg;

    bool has_gga_quality;
    uint8_t gga_quality;
    bool has_satellites;
    uint8_t satellites;
};

// Parser boundary (Material 7 section 13). Implementations must not expose library types.
class INmeaParser {
public:
    virtual ~INmeaParser() = default;
    // ESP_OK: parsed. ESP_ERR_NOT_SUPPORTED: sentence type not used. ESP_ERR_INVALID_RESPONSE: parse error.
    virtual esp_err_t consume_sentence(const char* sentence, size_t length, GpsFixUpdate* update) = 0;
};

// Standard NMEA-0183 RMC and GGA, any talker prefix. The actual sentence set emitted by the
// ATGM336H must be confirmed by the discovery test (NEEDS_HARDWARE_VALIDATION); no proprietary
// CASIC sentences or configuration commands are implemented.
class StandardNmeaParser final : public INmeaParser {
public:
    esp_err_t consume_sentence(const char* sentence, size_t length, GpsFixUpdate* update) override;
};

// Pure helpers (unit-tested).
// "ddmm.mmmm"/"dddmm.mmmm" + hemisphere -> signed degrees * 1e7.
bool nmea_parse_coordinate(const char* value, char hemisphere, bool is_latitude, int32_t& out_e7);
bool nmea_parse_time(const char* value, uint8_t& h, uint8_t& m, uint8_t& s, uint16_t& ms);
bool nmea_parse_date(const char* value, uint8_t& d, uint8_t& mo, uint16_t& y);
bool nmea_parse_decimal(const char* value, float& out);

}  // namespace cownect::sensors
