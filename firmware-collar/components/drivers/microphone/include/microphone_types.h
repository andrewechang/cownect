#pragma once

#include <cstddef>
#include <cstdint>

namespace cownect::sensors {

// Raw ADC code container (Material 8 section 10). Unsigned, never centered in place.
using MicRawSample = uint16_t;

// Material 8 section 13.
struct MicrophoneCaptureStats {
    uint32_t samples_stored;
    uint32_t adc_frames_received;
    uint32_t adc_results_seen;
    uint32_t wrong_channel_results;

    uint32_t driver_pool_overflow_count;
    uint32_t destination_overflow_count;
    uint32_t read_timeout_count;
    uint32_t adc_error_count;

    uint16_t raw_min;
    uint16_t raw_max;
    double raw_mean;
    double raw_rms_ac;  // sqrt(mean((x - mean)^2)); never non-centered RMS
    bool statistics_valid;

    uint32_t low_rail_count;
    uint32_t high_rail_count;

    uint64_t capture_start_us;
    uint64_t capture_end_us;
    double effective_sample_rate_hz;  // measured samples / measured elapsed time
};

struct MicCaptureBuffer {
    MicRawSample* data;  // preallocated PSRAM
    size_t capacity;
};

}  // namespace cownect::sensors
