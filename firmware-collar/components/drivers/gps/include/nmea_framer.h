#pragma once

#include <cstddef>
#include <cstdint>
#include "cownect_config.h"

namespace cownect::sensors {

// Bounded NMEA sentence framer (Material 7 section 11). Fixed buffer, no dynamic growth.
class NmeaFramer {
public:
    enum class Result { NONE, SENTENCE, CHECKSUM_ERROR, OVERLENGTH };

    // Feed one byte. On SENTENCE, body() returns the text between '$' and '*' (or end of line),
    // null-terminated, and checksum_present() tells whether a checksum was verified.
    Result push(char c);
    void reset();

    const char* body() const { return buf_; }
    size_t body_length() const { return body_len_; }
    bool checksum_present() const { return checksum_present_; }

    static uint8_t checksum(const char* body, size_t len);

private:
    Result finish_line();

    char buf_[config::GPS_MAX_SENTENCE_BYTES + 1] = {};
    size_t len_ = 0;
    size_t body_len_ = 0;
    bool in_sentence_ = false;
    bool checksum_present_ = false;
};

}  // namespace cownect::sensors
