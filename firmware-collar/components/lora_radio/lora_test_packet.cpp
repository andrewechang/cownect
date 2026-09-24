#include "lora_test_packet.h"

namespace cownect::radio {
namespace {
void put_le(uint8_t* p, uint64_t v, int bytes)
{
    for (int i = 0; i < bytes; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}
uint64_t get_le(const uint8_t* p, int bytes)
{
    uint64_t v = 0;
    for (int i = 0; i < bytes; ++i) v |= static_cast<uint64_t>(p[i]) << (8 * i);
    return v;
}
}  // namespace

size_t lora_test_packet_encode(const LoraTestPacket& p, uint8_t* dst, size_t capacity)
{
    if (dst == nullptr || capacity < LORA_TEST_PACKET_BYTES) {
        return 0;
    }
    put_le(dst + 0, LORA_TEST_MAGIC, 2);
    dst[2] = p.version;
    dst[3] = static_cast<uint8_t>(p.type);
    put_le(dst + 4, p.device_id, 8);
    put_le(dst + 12, p.sequence, 4);
    put_le(dst + 16, p.value, 4);
    return LORA_TEST_PACKET_BYTES;
}

bool lora_test_packet_decode(const uint8_t* src, size_t length, LoraTestPacket& out)
{
    if (src == nullptr || length != LORA_TEST_PACKET_BYTES) return false;
    if (get_le(src, 2) != LORA_TEST_MAGIC || src[2] != LORA_TEST_VERSION) return false;
    if (src[3] != static_cast<uint8_t>(LoraTestType::PING) && src[3] != static_cast<uint8_t>(LoraTestType::ACK)) {
        return false;
    }
    out.version = src[2];
    out.type = static_cast<LoraTestType>(src[3]);
    out.device_id = get_le(src + 4, 8);
    out.sequence = static_cast<uint32_t>(get_le(src + 12, 4));
    out.value = static_cast<uint32_t>(get_le(src + 16, 4));
    return true;
}

}  // namespace cownect::radio
