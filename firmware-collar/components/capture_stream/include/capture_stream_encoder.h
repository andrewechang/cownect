#pragma once

#include "byte_stream.h"
#include "capture_stream_format.h"
#include "capture_types.h"

namespace cownect::codec {

class ICaptureStreamEncoder : public IByteStream {
public:
    virtual esp_err_t prepare(const data::CaptureSession& capture) = 0;
};

// Exposes a frozen CaptureSession as the versioned byte stream without copying it
// (Material 13). Deterministic: identical reads for an unchanged capture. The capture must stay
// unmodified (FROZEN_FOR_TRANSFER) between prepare() and the last read().
class CaptureStreamEncoder final : public ICaptureStreamEncoder {
public:
    esp_err_t prepare(const data::CaptureSession& capture) override;
    uint32_t total_bytes() const override { return total_; }
    esp_err_t read(uint32_t offset, uint8_t* dst, size_t dst_capacity, size_t& bytes_written) const override;

private:
    enum class Kind : uint8_t { BLOB, ACCEL, GPS, BOARD, COW, MIC };
    struct Segment {
        uint32_t start;
        uint32_t length;
        Kind kind;
        uint16_t record_bytes;
        uint32_t blob_offset;
    };

    bool add_segment(Kind kind, uint64_t length, uint16_t record_bytes, uint32_t blob_offset, uint64_t& cursor);
    size_t encode_record(Kind kind, uint32_t index, uint8_t* out) const;
    void encode_diagnostics(uint8_t* out) const;

    const data::CaptureSession* cap_ = nullptr;
    uint32_t total_ = 0;
    Segment segs_[16] = {};
    size_t seg_count_ = 0;
    // Header + 6 section headers + diagnostics payload
    uint8_t blob_[stream::HEADER_BYTES + 6 * stream::SECTION_HEADER_BYTES + stream::DIAGNOSTICS_RECORD_BYTES] = {};
};

}  // namespace cownect::codec
