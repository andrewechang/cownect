#pragma once

#include <cstddef>
#include <cstdint>

// LORA_FULL fragment protocol, version 1 (Material 13 sections 26-34). Little-endian.
namespace cownect::lorafull {

inline constexpr uint16_t LORA_FULL_MAGIC = 0x4643;  // bytes "CF"
inline constexpr uint8_t LORA_FULL_PROTOCOL_VERSION = 1;

enum class PacketType : uint8_t { FULL_DATA = 1, FULL_ACK = 2 };

// FULL_DATA header, 40 bytes:
//  u16 magic | u8 protocol_version | u8 packet_type | u64 device_id | u32 capture_id |
//  u32 fragment_index | u32 fragment_count | u32 stream_offset | u32 total_stream_bytes |
//  u16 payload_bytes | u16 flags | u32 stream_crc32 (valid when flags bit0 set)
// Fragment indices run 0 .. fragment_count-1.
inline constexpr size_t DATA_HEADER_BYTES = 40;
inline constexpr uint16_t FLAG_STREAM_CRC32_PRESENT = 0x0001;
inline constexpr uint16_t FLAG_LAST_FRAGMENT = 0x0002;

struct DataHeader {
    uint8_t protocol_version;
    PacketType packet_type;
    uint64_t device_id;
    uint32_t capture_id;
    uint32_t fragment_index;
    uint32_t fragment_count;
    uint32_t stream_offset;
    uint32_t total_stream_bytes;
    uint16_t payload_bytes;
    uint16_t flags;
    uint32_t stream_crc32;
};

// FULL_ACK, 21 bytes: u16 magic | u8 version | u8 type | u64 device_id | u32 capture_id |
//                     u32 fragment_index | u8 status (0 = OK)
inline constexpr size_t ACK_BYTES = 21;

struct Ack {
    uint64_t device_id;
    uint32_t capture_id;
    uint32_t fragment_index;
    uint8_t status;
};

size_t encode_data_header(const DataHeader& h, uint8_t* dst, size_t capacity);
bool decode_data_header(const uint8_t* src, size_t length, DataHeader& out);
// Validates index/offset/length bounds against the header's own totals.
bool data_header_bounds_ok(const DataHeader& h, size_t packet_length);

size_t encode_ack(const Ack& a, uint8_t* dst, size_t capacity);
bool decode_ack(const uint8_t* src, size_t length, Ack& out);

}  // namespace cownect::lorafull
