#include "nmea_framer.h"

namespace cownect::sensors {

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

uint8_t NmeaFramer::checksum(const char* body, size_t len)
{
    uint8_t x = 0;
    for (size_t i = 0; i < len; ++i) {
        x ^= static_cast<uint8_t>(body[i]);
    }
    return x;
}

void NmeaFramer::reset()
{
    len_ = 0;
    body_len_ = 0;
    in_sentence_ = false;
    checksum_present_ = false;
    buf_[0] = '\0';
}

NmeaFramer::Result NmeaFramer::push(char c)
{
    if (c == '$') {
        // Start of a new sentence; any partial line is abandoned.
        len_ = 0;
        in_sentence_ = true;
        return Result::NONE;
    }
    if (!in_sentence_) {
        return Result::NONE;
    }
    if (c == '\r') {
        return Result::NONE;
    }
    if (c == '\n') {
        in_sentence_ = false;
        return finish_line();
    }
    if (len_ >= sizeof(buf_) - 1) {
        reset();  // malformed overlength input: wait for next '$'
        return Result::OVERLENGTH;
    }
    buf_[len_++] = c;
    return Result::NONE;
}

NmeaFramer::Result NmeaFramer::finish_line()
{
    buf_[len_] = '\0';
    size_t star = len_;
    for (size_t i = 0; i < len_; ++i) {
        if (buf_[i] == '*') {
            star = i;
            break;
        }
    }
    checksum_present_ = false;
    if (star < len_) {
        if (len_ - star != 3) {
            return Result::CHECKSUM_ERROR;
        }
        const int hi = hex_value(buf_[star + 1]);
        const int lo = hex_value(buf_[star + 2]);
        if (hi < 0 || lo < 0 || checksum(buf_, star) != static_cast<uint8_t>((hi << 4) | lo)) {
            return Result::CHECKSUM_ERROR;
        }
        checksum_present_ = true;
    }
    buf_[star] = '\0';
    body_len_ = star;
    return body_len_ > 0 ? Result::SENTENCE : Result::NONE;
}

}  // namespace cownect::sensors
