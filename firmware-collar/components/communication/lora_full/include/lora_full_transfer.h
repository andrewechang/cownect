#pragma once

#include <cstdint>
#include "byte_stream.h"
#include "capture_stream_encoder.h"
#include "capture_types.h"
#include "esp_err.h"
#include "lora_full_protocol.h"
#include "sx1262_adapter.h"

namespace cownect::lorafull {

// Material 13 sections 35-36. RF settings stay in the Material 12 LoraRfProfile.
struct LoraFullReliabilityConfig {
    bool ack_enabled;
    uint32_t ack_timeout_ms;
    uint8_t max_retries_per_fragment;  // 0 = send once
};

struct LoraFullConfig {
    uint8_t protocol_version;
    uint16_t fragment_payload_bytes;
    bool stream_crc32_enabled;
    LoraFullReliabilityConfig reliability;
};

// Loads from menuconfig. COWNECT_ERR_NOT_CONFIGURED when fragment size (or ACK timeout/retries
// in ACK mode) is not set. Rejects header + payload > radio packet limit.
esp_err_t lora_full_load_config(LoraFullConfig& out, const char** missing);

// Fragmenter (Material 13 section 24): builds packets from a stream; no SPI, no radio.
class CaptureFragmenter {
public:
    esp_err_t begin(const codec::IByteStream& stream, const LoraFullConfig& cfg, uint64_t device_id,
                    uint32_t capture_id, bool crc_present, uint32_t stream_crc32);
    uint32_t fragment_count() const { return fragment_count_; }
    esp_err_t build_fragment(uint32_t fragment_index, uint8_t* dst, size_t dst_capacity, size_t& packet_bytes) const;

private:
    const codec::IByteStream* stream_ = nullptr;
    LoraFullConfig cfg_ = {};
    uint64_t device_id_ = 0;
    uint32_t capture_id_ = 0;
    bool crc_present_ = false;
    uint32_t crc_ = 0;
    uint32_t fragment_count_ = 0;
};

// Material 13 section 37.
struct LoraFullTransferStats {
    uint64_t device_id;
    uint32_t capture_id;

    uint32_t stream_bytes;
    uint32_t fragment_payload_bytes;
    uint32_t fragment_count;

    uint32_t fragments_tx_started;
    uint32_t fragments_tx_done;
    uint32_t fragments_failed;
    uint32_t failed_fragment_index;

    uint32_t ack_received;
    uint32_t ack_timeout_count;
    uint32_t retry_count;

    uint32_t radio_error_count;
    uint32_t abort_count;

    uint64_t transfer_start_us;
    uint64_t transfer_end_us;
    uint32_t communication_time_ms;

    bool stream_crc32_present;
    uint32_t stream_crc32;
    bool transfer_complete_sender_side;  // NOT receiver completeness
};

enum class LoraFullState : uint8_t {
    IDLE,
    PREPARING_STREAM,
    BUILDING_FRAGMENT,
    STARTING_TX,
    WAITING_TX_DONE,
    WAITING_ACK,
    ADVANCING,
    COMPLETE,
    ABORTED,
    ERROR
};

class ILoraFullTransfer {
public:
    virtual ~ILoraFullTransfer() = default;
    virtual esp_err_t begin(const data::CaptureSession& capture) = 0;
    virtual esp_err_t service() = 0;
    virtual bool in_progress() const = 0;
    virtual bool complete() const = 0;
    virtual esp_err_t abort() = 0;
    virtual const LoraFullTransferStats& stats() const = 0;
};

class LoraFullTransfer final : public ILoraFullTransfer {
public:
    explicit LoraFullTransfer(radio::Sx1262Adapter& radio) : radio_(radio) {}

    esp_err_t begin(const data::CaptureSession& capture) override;
    // Synthetic/development streams (Material 13 tests 13.3/13.4) use the same path.
    esp_err_t begin_stream(const codec::IByteStream& stream, uint64_t device_id, uint32_t capture_id);
    esp_err_t service() override;
    bool in_progress() const override;
    bool complete() const override { return state_ == LoraFullState::COMPLETE; }
    esp_err_t abort() override;
    const LoraFullTransferStats& stats() const override { return stats_; }
    LoraFullState state() const { return state_; }

    // Runs service() until COMPLETE/ABORTED/ERROR. No cut-off at the 120 s cycle boundary:
    // bounded only by the per-fragment timeout/retry policy (Material 13 section 45).
    esp_err_t run_to_completion();

    // Development: drop the first transmission of every Nth fragment (0 = off) to exercise
    // retry/missing-fragment accounting without touching SX1262 commands (test 13.7).
    void set_skip_every_nth_for_test(uint32_t n) { skip_every_nth_ = n; }

private:
    esp_err_t start_common(const codec::IByteStream& stream, uint64_t device_id, uint32_t capture_id);
    void fail_fragment();
    void finish(LoraFullState final_state);

    radio::Sx1262Adapter& radio_;
    codec::CaptureStreamEncoder encoder_;
    CaptureFragmenter fragmenter_;
    LoraFullConfig cfg_ = {};
    LoraFullTransferStats stats_ = {};
    LoraFullState state_ = LoraFullState::IDLE;
    uint32_t index_ = 0;
    uint8_t attempts_ = 0;
    uint32_t last_progress_decile_ = 0;
    uint32_t skip_every_nth_ = 0;
    uint8_t packet_[255] = {};
    size_t packet_len_ = 0;
};

}  // namespace cownect::lorafull
