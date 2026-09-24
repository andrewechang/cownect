#pragma once

#include <cstddef>
#include <cstdint>

namespace cownect::codec {

// CRC-32/ISO-HDLC (the zlib / Python binascii.crc32 variant):
// reflected polynomial 0xEDB88320, init 0xFFFFFFFF, final XOR 0xFFFFFFFF.
// Check value: crc32("123456789") == 0xCBF43926.
uint32_t crc32_update(uint32_t state, const uint8_t* data, size_t len);  // state starts at crc32_init()
inline constexpr uint32_t crc32_init() { return 0xFFFFFFFFu; }
inline constexpr uint32_t crc32_final(uint32_t state) { return state ^ 0xFFFFFFFFu; }
uint32_t crc32(const uint8_t* data, size_t len);

}  // namespace cownect::codec
