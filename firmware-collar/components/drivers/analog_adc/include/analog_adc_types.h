#pragma once

#include <cstddef>
#include <cstdint>

namespace cownect::sensors {

enum class AnalogChannelId : uint8_t { MIC = 0, BOARD_TEMP = 1, COW_TEMP = 2 };

// Material 9 section 14.
struct IntegratedAdcStats {
    uint64_t total_results;
    uint64_t microphone_results;
    uint64_t board_temp_results;
    uint64_t cow_temp_results;

    uint32_t frames_received;
    uint32_t invalid_results;
    uint32_t unexpected_channel_results;
    uint32_t pattern_order_errors;
    uint32_t pool_overflow_count;
    uint32_t read_timeout_count;
    uint32_t driver_error_count;
    uint32_t bucket_queue_overflow;

    // Trailing partial 1 s bucket (< 1 s at capture stop) is NOT converted into a logical
    // temperature sample; its conversion counts are kept here for diagnostics.
    uint32_t board_partial_bucket_conversions;
    uint32_t cow_partial_bucket_conversions;

    uint32_t sample_freq_hz;
    uint8_t pattern_len;
    uint8_t observed_first_channels[16];  // raw ADC channel numbers in arrival order
    uint8_t observed_count;
};

// Raw microphone stream statistics accumulated in the acquisition task (exact integer sums).
struct MicStreamStats {
    uint32_t samples_stored;
    uint32_t destination_overflow_count;
    uint64_t results_seen;
    uint64_t raw_sum;
    uint64_t raw_sum_sq;
    uint16_t raw_min;
    uint16_t raw_max;
    uint32_t low_rail_count;
    uint32_t high_rail_count;
};

// One completed 1-second logical temperature bucket (Material 9 section 9).
// Timestamp convention (documented, identical for both channels): bucket END offset.
struct AnalogBucketResult {
    AnalogChannelId channel;
    uint32_t bucket_index;
    uint32_t end_offset_ms;
    uint32_t raw_count;
    uint16_t raw_mean;  // arithmetic mean, rounded; valid only if raw_count > 0
    uint16_t raw_min;
    uint16_t raw_max;
    uint32_t near_low_count;
    uint32_t near_high_count;
};

}  // namespace cownect::sensors
