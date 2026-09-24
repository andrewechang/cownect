#include "lora_full_protocol.h"

#include "byte_codec.h"

namespace cownect::lorafull {

size_t encode_data_header(const DataHeader& h, uint8_t* dst, size_t capacity)
{
    codec::ByteWriter w(dst, capacity);
    w.u16(LORA_FULL_MAGIC);
    w.u8(h.protocol_version);
    w.u8(static_cast<uint8_t>(h.packet_type));
    w.u64(h.device_id);
    w.u32(h.capture_id);
    w.u32(h.fragment_index);
    w.u32(h.fragment_count);
    w.u32(h.stream_offset);
    w.u32(h.total_stream_bytes);
    w.u16(h.payload_bytes);
    w.u16(h.flags);
    w.u32(h.stream_crc32);
    return w.overflow() ? 0 : w.position();
}

bool decode_data_header(const uint8_t* src, size_t length, DataHeader& h)
{
    if (src == nullptr || length < DATA_HEADER_BYTES) return false;
    codec::ByteReader r(src, length);
    if (r.u16() != LORA_FULL_MAGIC) return false;
    h.protocol_version = r.u8();
    h.packet_type = static_cast<PacketType>(r.u8());
    h.device_id = r.u64();
    h.capture_id = r.u32();
    h.fragment_index = r.u32();
    h.fragment_count = r.u32();
    h.stream_offset = r.u32();
    h.total_stream_bytes = r.u32();
    h.payload_bytes = r.u16();
    h.flags = r.u16();
    h.stream_crc32 = r.u32();
    return !r.error() && h.protocol_version == LORA_FULL_PROTOCOL_VERSION && h.packet_type == PacketType::FULL_DATA;
}

bool data_header_bounds_ok(const DataHeader& h, size_t packet_length)
{
    if (h.fragment_count == 0 || h.fragment_index >= h.fragment_count) return false;
    if (packet_length != DATA_HEADER_BYTES + h.payload_bytes) return false;
    if (static_cast<uint64_t>(h.stream_offset) + h.payload_bytes > h.total_stream_bytes) return false;
    return true;
}

size_t encode_ack(const Ack& a, uint8_t* dst, size_t capacity)
{
    codec::ByteWriter w(dst, capacity);
    w.u16(LORA_FULL_MAGIC);
    w.u8(LORA_FULL_PROTOCOL_VERSION);
    w.u8(static_cast<uint8_t>(PacketType::FULL_ACK));
    w.u64(a.device_id);
    w.u32(a.capture_id);
    w.u32(a.fragment_index);
    w.u8(a.status);
    return w.overflow() ? 0 : w.position();
}

bool decode_ack(const uint8_t* src, size_t length, Ack& a)
{
    if (src == nullptr || length != ACK_BYTES) return false;
    codec::ByteReader r(src, length);
    if (r.u16() != LORA_FULL_MAGIC || r.u8() != LORA_FULL_PROTOCOL_VERSION ||
        r.u8() != static_cast<uint8_t>(PacketType::FULL_ACK)) {
        return false;
    }
    a.device_id = r.u64();
    a.capture_id = r.u32();
    a.fragment_index = r.u32();
    a.status = r.u8();
    return !r.error();
}

}  // namespace cownect::lorafull
