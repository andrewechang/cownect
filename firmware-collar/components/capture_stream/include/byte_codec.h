#pragma once

// Explicit little-endian application codec (Material 13 section 10).
// Every multi-byte field is written byte by byte; host struct layout is never used.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace cownect::codec {

static_assert(std::numeric_limits<float>::is_iec559 && sizeof(float) == 4,
              "float32 fields require IEEE-754 binary32 (Material 13 section 10)");

class ByteWriter {
public:
    ByteWriter(uint8_t* dst, size_t capacity) : p_(dst), cap_(capacity) {}

    void u8(uint8_t v) { put(&v, 1); }
    void u16(uint16_t v)
    {
        const uint8_t b[2] = {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8)};
        put(b, 2);
    }
    void u32(uint32_t v)
    {
        const uint8_t b[4] = {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v >> 16),
                              static_cast<uint8_t>(v >> 24)};
        put(b, 4);
    }
    void u64(uint64_t v)
    {
        u32(static_cast<uint32_t>(v));
        u32(static_cast<uint32_t>(v >> 32));
    }
    void i16(int16_t v) { u16(static_cast<uint16_t>(v)); }
    void i32(int32_t v) { u32(static_cast<uint32_t>(v)); }
    void f32(float v)
    {
        uint32_t bits;
        std::memcpy(&bits, &v, sizeof(bits));
        u32(bits);
    }
    void zeros(size_t n)
    {
        for (size_t i = 0; i < n; ++i) u8(0);
    }

    size_t position() const { return pos_; }
    bool overflow() const { return overflow_; }

private:
    void put(const uint8_t* b, size_t n)
    {
        if (pos_ + n > cap_) {
            overflow_ = true;
            return;
        }
        std::memcpy(p_ + pos_, b, n);
        pos_ += n;
    }
    uint8_t* p_;
    size_t cap_;
    size_t pos_ = 0;
    bool overflow_ = false;
};

class ByteReader {
public:
    ByteReader(const uint8_t* src, size_t length) : p_(src), len_(length) {}

    uint8_t u8() { uint8_t b[1] = {}; get(b, 1); return b[0]; }
    uint16_t u16()
    {
        uint8_t b[2] = {};
        get(b, 2);
        return static_cast<uint16_t>(b[0] | (b[1] << 8));
    }
    uint32_t u32()
    {
        uint8_t b[4] = {};
        get(b, 4);
        return static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8) | (static_cast<uint32_t>(b[2]) << 16) |
               (static_cast<uint32_t>(b[3]) << 24);
    }
    uint64_t u64()
    {
        const uint64_t lo = u32();
        const uint64_t hi = u32();
        return lo | (hi << 32);
    }
    int16_t i16() { return static_cast<int16_t>(u16()); }
    int32_t i32() { return static_cast<int32_t>(u32()); }
    float f32()
    {
        const uint32_t bits = u32();
        float v;
        std::memcpy(&v, &bits, sizeof(v));
        return v;
    }
    void skip(size_t n)
    {
        if (pos_ + n > len_) { error_ = true; pos_ = len_; return; }
        pos_ += n;
    }

    size_t position() const { return pos_; }
    bool error() const { return error_; }

private:
    void get(uint8_t* b, size_t n)
    {
        if (pos_ + n > len_) {
            error_ = true;
            return;
        }
        std::memcpy(b, p_ + pos_, n);
        pos_ += n;
    }
    const uint8_t* p_;
    size_t len_;
    size_t pos_ = 0;
    bool error_ = false;
};

}  // namespace cownect::codec
