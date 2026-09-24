#pragma once

#include <cstddef>
#include <cstdint>
#include "esp_err.h"

namespace cownect::codec {

// Deterministic random-access byte stream. Consumers (LoRa fragmenter, Wi-Fi uploader) pull
// small chunks: no multi-megabyte serialized copy is ever built (Material 13 section 8).
class IByteStream {
public:
    virtual ~IByteStream() = default;
    virtual uint32_t total_bytes() const = 0;
    // Copies up to dst_capacity bytes starting at offset. offset == total -> 0 bytes, ESP_OK.
    virtual esp_err_t read(uint32_t offset, uint8_t* dst, size_t dst_capacity, size_t& bytes_written) const = 0;
};

// CRC32 over the whole stream using a small stack buffer.
esp_err_t stream_crc32(const IByteStream& stream, uint32_t& crc_out);

}  // namespace cownect::codec
