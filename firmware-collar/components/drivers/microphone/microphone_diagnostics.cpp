#include <cmath>
#include "microphone_capture_driver.h"

namespace cownect::sensors {

MicrophoneCaptureStats microphone_stats_from_engine(const MicStreamStats& mic, const IntegratedAdcStats& adc,
                                                    uint64_t start_us, uint64_t end_us)
{
    MicrophoneCaptureStats s = {};
    s.samples_stored = mic.samples_stored;
    s.adc_frames_received = adc.frames_received;
    s.adc_results_seen = static_cast<uint32_t>(mic.results_seen);
    s.wrong_channel_results = adc.unexpected_channel_results + adc.invalid_results;
    s.driver_pool_overflow_count = adc.pool_overflow_count;
    s.destination_overflow_count = mic.destination_overflow_count;
    s.read_timeout_count = adc.read_timeout_count;
    s.adc_error_count = adc.driver_error_count;
    s.low_rail_count = mic.low_rail_count;
    s.high_rail_count = mic.high_rail_count;
    s.capture_start_us = start_us;
    s.capture_end_us = end_us;
    if (mic.samples_stored > 0) {
        const double n = static_cast<double>(mic.samples_stored);
        const double mean = static_cast<double>(mic.raw_sum) / n;
        double var = static_cast<double>(mic.raw_sum_sq) / n - mean * mean;
        if (var < 0.0) var = 0.0;  // rounding guard
        s.raw_mean = mean;
        s.raw_rms_ac = std::sqrt(var);
        s.raw_min = mic.raw_min;
        s.raw_max = mic.raw_max;
        s.statistics_valid = true;
    }
    if (end_us > start_us) {
        s.effective_sample_rate_hz = static_cast<double>(mic.samples_stored) * 1e6 / static_cast<double>(end_us - start_us);
    }
    return s;
}

}  // namespace cownect::sensors
