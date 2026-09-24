#include "cownect_config.h"
#include "lora_full_transfer.h"

namespace cownect::lorafull {

esp_err_t CaptureFragmenter::begin(const codec::IByteStream& stream, const LoraFullConfig& cfg, uint64_t device_id,
                                   uint32_t capture_id, bool crc_present, uint32_t stream_crc32)
{
    stream_ = nullptr;
    fragment_count_ = 0;
    if (cfg.fragment_payload_bytes == 0 ||
        DATA_HEADER_BYTES + cfg.fragment_payload_bytes > config::SX126X_MAX_PAYLOAD_BYTES) {
        return ESP_ERR_INVALID_SIZE;  // oversized configuration rejected
    }
    const uint32_t total = stream.total_bytes();
    if (total == 0) {
        return ESP_ERR_INVALID_SIZE;
    }
    stream_ = &stream;
    cfg_ = cfg;
    device_id_ = device_id;
    capture_id_ = capture_id;
    crc_present_ = crc_present;
    crc_ = stream_crc32;
    fragment_count_ = (total + cfg.fragment_payload_bytes - 1) / cfg.fragment_payload_bytes;
    return ESP_OK;
}

esp_err_t CaptureFragmenter::build_fragment(uint32_t idx, uint8_t* dst, size_t cap, size_t& packet_bytes) const
{
    packet_bytes = 0;
    if (stream_ == nullptr || idx >= fragment_count_) {
        return ESP_ERR_INVALID_ARG;
    }
    const uint32_t total = stream_->total_bytes();
    const uint32_t offset = idx * static_cast<uint32_t>(cfg_.fragment_payload_bytes);
    const uint32_t remaining = total - offset;
    const uint16_t payload =
        static_cast<uint16_t>(remaining < cfg_.fragment_payload_bytes ? remaining : cfg_.fragment_payload_bytes);
    if (cap < DATA_HEADER_BYTES + payload) {
        return ESP_ERR_INVALID_SIZE;
    }
    DataHeader h = {};
    h.protocol_version = cfg_.protocol_version;
    h.packet_type = PacketType::FULL_DATA;
    h.device_id = device_id_;
    h.capture_id = capture_id_;
    h.fragment_index = idx;
    h.fragment_count = fragment_count_;
    h.stream_offset = offset;
    h.total_stream_bytes = total;
    h.payload_bytes = payload;
    h.flags = static_cast<uint16_t>((crc_present_ ? FLAG_STREAM_CRC32_PRESENT : 0) |
                                    (idx + 1 == fragment_count_ ? FLAG_LAST_FRAGMENT : 0));
    h.stream_crc32 = crc_present_ ? crc_ : 0;
    if (encode_data_header(h, dst, cap) != DATA_HEADER_BYTES) {
        return ESP_FAIL;
    }
    size_t got = 0;
    esp_err_t err = stream_->read(offset, dst + DATA_HEADER_BYTES, payload, got);
    if (err != ESP_OK || got != payload) {
        return err != ESP_OK ? err : ESP_FAIL;
    }
    packet_bytes = DATA_HEADER_BYTES + payload;
    return ESP_OK;
}

}  // namespace cownect::lorafull
