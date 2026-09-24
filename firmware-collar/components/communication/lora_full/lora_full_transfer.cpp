#include "lora_full_transfer.h"

#include "cownect_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lora_config.h"

namespace cownect::lorafull {
namespace {
constexpr const char* TAG = "LORA_FULL";
constexpr uint32_t kEventWaitMs = 50;  // bounded wait slice; keeps the task watchdog-safe
}  // namespace

esp_err_t LoraFullTransfer::begin(const data::CaptureSession& capture)
{
    esp_err_t err = encoder_.prepare(capture);
    if (err != ESP_OK) {
        return err;
    }
    return start_common(encoder_, capture.id.device_id, capture.id.capture_id);
}

esp_err_t LoraFullTransfer::begin_stream(const codec::IByteStream& stream, uint64_t device_id, uint32_t capture_id)
{
    return start_common(stream, device_id, capture_id);
}

esp_err_t LoraFullTransfer::start_common(const codec::IByteStream& stream, uint64_t device_id, uint32_t capture_id)
{
    if (in_progress()) {
        return ESP_ERR_INVALID_STATE;
    }
    const char* missing = nullptr;
    esp_err_t err = lora_full_load_config(cfg_, &missing);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "not started: %s (%s)", cownect_err_name(err), missing);
        return err;
    }
    const char* reason = nullptr;
    err = radio::lora_tx_gate(&reason);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "not started: %s", reason);
        return err;
    }
    if (!radio_.profile_applied()) {
        return ESP_ERR_INVALID_STATE;
    }
    state_ = LoraFullState::PREPARING_STREAM;
    stats_ = {};
    stats_.device_id = device_id;
    stats_.capture_id = capture_id;
    stats_.stream_bytes = stream.total_bytes();
    stats_.fragment_payload_bytes = cfg_.fragment_payload_bytes;
    stats_.transfer_start_us = static_cast<uint64_t>(esp_timer_get_time());
    uint32_t crc = 0;
    if (cfg_.stream_crc32_enabled) {
        err = codec::stream_crc32(stream, crc);
        if (err != ESP_OK) {
            finish(LoraFullState::ERROR);
            return err;
        }
        stats_.stream_crc32_present = true;
        stats_.stream_crc32 = crc;
    }
    err = fragmenter_.begin(stream, cfg_, device_id, capture_id, cfg_.stream_crc32_enabled, crc);
    if (err != ESP_OK) {
        finish(LoraFullState::ERROR);
        return err;
    }
    stats_.fragment_count = fragmenter_.fragment_count();
    index_ = 0;
    attempts_ = 0;
    last_progress_decile_ = 0;
    state_ = LoraFullState::BUILDING_FRAGMENT;
    ESP_LOGI(TAG, "capture=%u bytes=%u fragments=%u payload=%u ack=%d crc32=%d", static_cast<unsigned>(capture_id),
             static_cast<unsigned>(stats_.stream_bytes), static_cast<unsigned>(stats_.fragment_count),
             static_cast<unsigned>(cfg_.fragment_payload_bytes), cfg_.reliability.ack_enabled,
             cfg_.stream_crc32_enabled);
    return ESP_OK;
}

bool LoraFullTransfer::in_progress() const
{
    return state_ != LoraFullState::IDLE && state_ != LoraFullState::COMPLETE && state_ != LoraFullState::ABORTED &&
           state_ != LoraFullState::ERROR;
}

void LoraFullTransfer::finish(LoraFullState final_state)
{
    state_ = final_state;
    stats_.transfer_end_us = static_cast<uint64_t>(esp_timer_get_time());
    stats_.communication_time_ms = static_cast<uint32_t>((stats_.transfer_end_us - stats_.transfer_start_us) / 1000);
    stats_.transfer_complete_sender_side = final_state == LoraFullState::COMPLETE;
    if (radio_.tx_in_progress() || radio_.rx_in_progress()) {
        radio_.enter_standby();
    }
    ESP_LOGI(TAG, "done state=%u comm_ms=%u tx_done=%u failed=%u retries=%u ack_timeouts=%u sender_complete=%d",
             static_cast<unsigned>(final_state), static_cast<unsigned>(stats_.communication_time_ms),
             static_cast<unsigned>(stats_.fragments_tx_done), static_cast<unsigned>(stats_.fragments_failed),
             static_cast<unsigned>(stats_.retry_count), static_cast<unsigned>(stats_.ack_timeout_count),
             stats_.transfer_complete_sender_side);
}

void LoraFullTransfer::fail_fragment()
{
    // Retry policy: retry up to max_retries_per_fragment, then abort the remaining transfer
    // (Material 13 section 44). Later fragments never make an incomplete transfer complete.
    if (attempts_ <= cfg_.reliability.max_retries_per_fragment) {
        stats_.retry_count++;
        state_ = LoraFullState::STARTING_TX;
        return;
    }
    stats_.fragments_failed++;
    stats_.failed_fragment_index = index_;
    stats_.abort_count++;
    finish(LoraFullState::ABORTED);
}

esp_err_t LoraFullTransfer::service()
{
    switch (state_) {
    case LoraFullState::BUILDING_FRAGMENT: {
        esp_err_t err = fragmenter_.build_fragment(index_, packet_, sizeof(packet_), packet_len_);
        if (err != ESP_OK) {
            finish(LoraFullState::ERROR);
            return err;
        }
        attempts_ = 0;
        state_ = LoraFullState::STARTING_TX;
        return ESP_OK;
    }
    case LoraFullState::STARTING_TX: {
        attempts_++;
        stats_.fragments_tx_started++;
        if (skip_every_nth_ > 0 && attempts_ == 1 && (index_ % skip_every_nth_) == 0) {
            // Development fault injection: this attempt is deliberately not transmitted.
            state_ = cfg_.reliability.ack_enabled ? LoraFullState::WAITING_ACK : LoraFullState::ADVANCING;
            if (cfg_.reliability.ack_enabled) {
                stats_.ack_timeout_count++;
                fail_fragment();
            }
            return ESP_OK;
        }
        esp_err_t err = radio_.send_async(packet_, packet_len_);
        if (err != ESP_OK) {
            stats_.radio_error_count++;
            fail_fragment();
            return ESP_OK;
        }
        state_ = LoraFullState::WAITING_TX_DONE;
        return ESP_OK;
    }
    case LoraFullState::WAITING_TX_DONE: {
        radio_.wait_for_event(kEventWaitMs);
        if (radio_.tx_in_progress()) {
            return ESP_OK;
        }
        if (radio_.last_tx_result() != radio::LoraOpResult::OK) {
            stats_.radio_error_count++;
            fail_fragment();
            return ESP_OK;
        }
        stats_.fragments_tx_done++;
        if (!cfg_.reliability.ack_enabled) {
            state_ = LoraFullState::ADVANCING;
            return ESP_OK;
        }
        if (radio_.start_receive(cfg_.reliability.ack_timeout_ms) != ESP_OK) {
            stats_.radio_error_count++;
            fail_fragment();
            return ESP_OK;
        }
        state_ = LoraFullState::WAITING_ACK;
        return ESP_OK;
    }
    case LoraFullState::WAITING_ACK: {
        radio_.wait_for_event(kEventWaitMs);
        if (radio_.rx_in_progress()) {
            return ESP_OK;
        }
        uint8_t buf[64];
        radio::LoraPacketRx meta = {};
        Ack ack = {};
        if (radio_.take_last_rx(buf, sizeof(buf), meta) && decode_ack(buf, meta.length, ack) &&
            ack.device_id == stats_.device_id && ack.capture_id == stats_.capture_id && ack.fragment_index == index_ &&
            ack.status == 0) {
            stats_.ack_received++;
            state_ = LoraFullState::ADVANCING;
        } else {
            stats_.ack_timeout_count++;
            fail_fragment();
        }
        return ESP_OK;
    }
    case LoraFullState::ADVANCING: {
        index_++;
        const uint32_t decile = index_ * 10 / stats_.fragment_count;
        if (decile != last_progress_decile_) {
            last_progress_decile_ = decile;
            ESP_LOGI(TAG, "progress=%u%% tx=%u retry=%u", static_cast<unsigned>(decile * 10),
                     static_cast<unsigned>(stats_.fragments_tx_done), static_cast<unsigned>(stats_.retry_count));
        }
        if (index_ >= stats_.fragment_count) {
            finish(LoraFullState::COMPLETE);
        } else {
            state_ = LoraFullState::BUILDING_FRAGMENT;
        }
        return ESP_OK;
    }
    default:
        return ESP_OK;
    }
}

esp_err_t LoraFullTransfer::abort()
{
    if (!in_progress()) {
        return ESP_OK;
    }
    stats_.abort_count++;
    finish(LoraFullState::ABORTED);
    return ESP_OK;
}

esp_err_t LoraFullTransfer::run_to_completion()
{
    uint32_t loops = 0;
    while (in_progress()) {
        esp_err_t err = service();
        if (err != ESP_OK && !in_progress()) {
            return err;
        }
        if (++loops % 32 == 0) {
            vTaskDelay(1);  // yield between fragments for watchdog/console responsiveness
        }
    }
    return complete() ? ESP_OK : COWNECT_ERR_INCOMPLETE;
}

}  // namespace cownect::lorafull
