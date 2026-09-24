#pragma once

#include <cstddef>
#include <cstdint>

namespace cownect::sensors {

// Parsed GNSS fix (Material 7 section 7). Data-layer representation, not a wire format.
// GNSS UTC is exposed with validity flags only; it never sets the ESP32 system clock.
struct GpsFix {
    uint32_t capture_offset_ms;

    int32_t latitude_e7;
    int32_t longitude_e7;

    float speed_mps;
    float course_deg;

    uint8_t fix_quality;  // GGA quality indicator when GGA was observed, else 0
    uint8_t satellites;   // GGA satellites in use when observed, else 0

    bool valid;
    bool utc_valid;       // time-of-day fields below are valid
    bool date_valid;      // date fields below are valid (RMC)

    uint8_t utc_hour;
    uint8_t utc_minute;
    uint8_t utc_second;
    uint16_t utc_millisecond;
    uint8_t utc_day;
    uint8_t utc_month;
    uint16_t utc_year;
};

struct GpsCaptureStats {
    uint32_t uart_bytes_received;
    uint32_t uart_overflow_count;
    uint32_t framed_sentence_count;
    uint32_t checksum_error_count;
    uint32_t parse_error_count;
    uint32_t valid_fix_count;
    uint32_t invalid_fix_count;
    uint32_t dropped_fix_count;
    uint32_t unsupported_sentence_count;
    uint32_t overlength_count;
    uint32_t duplicate_epoch_count;

    uint64_t capture_start_us;
    uint64_t capture_end_us;
    uint64_t last_byte_us;  // 0 if no byte seen (used to detect UART inactivity)
};

// Sentence-discovery statistics (Material 7 section 12).
struct GpsSentenceStat {
    char identifier[8];  // e.g. "GNRMC" (talker + type as observed)
    uint32_t count;
    uint32_t checksum_ok;
};

}  // namespace cownect::sensors
