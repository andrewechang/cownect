#pragma once

#include <cstddef>
#include <cstdint>

namespace cownect::radio {

// Development-only link test packet (Material 12 section 20). Not the LORA_FULL format.
// Explicit little-endian layout, 20 bytes:
//   u16 magic (0x5443, bytes "CT") | u8 version (1) | u8 type (1 PING, 2 ACK) |
//   u64 device_id | u32 sequence | u32 value
inline constexpr uint16_t LORA_TEST_MAGIC = 0x5443;
inline constexpr uint8_t LORA_TEST_VERSION = 1;
inline constexpr size_t LORA_TEST_PACKET_BYTES = 20;

enum class LoraTestType : uint8_t { PING = 1, ACK = 2 };

struct LoraTestPacket {
    uint8_t version;
    LoraTestType type;
    uint64_t device_id;
    uint32_t sequence;
    uint32_t value;
};

size_t lora_test_packet_encode(const LoraTestPacket& p, uint8_t* dst, size_t capacity);
bool lora_test_packet_decode(const uint8_t* src, size_t length, LoraTestPacket& out);

}  // namespace cownect::radio
