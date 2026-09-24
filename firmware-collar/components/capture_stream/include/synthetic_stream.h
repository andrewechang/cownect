#pragma once

#include "byte_stream.h"

namespace cownect::codec {

// Deterministic generated stream for development transfer tests (Material 13 test 13.3,
// Material 14 test 14.3). Byte i = f(i, seed); the Jetson/gateway test tools can regenerate it.
//   byte(i) = (i * 31 + (i >> 8) + seed) & 0xFF
class SyntheticStream final : public IByteStream {
public:
    SyntheticStream(uint32_t total_bytes, uint8_t seed) : total_(total_bytes), seed_(seed) {}
    uint32_t total_bytes() const override { return total_; }
    esp_err_t read(uint32_t offset, uint8_t* dst, size_t dst_capacity, size_t& bytes_written) const override;
    static uint8_t byte_at(uint32_t i, uint8_t seed)
    {
        return static_cast<uint8_t>(i * 31u + (i >> 8) + seed);
    }

private:
    uint32_t total_;
    uint8_t seed_;
};

}  // namespace cownect::codec
