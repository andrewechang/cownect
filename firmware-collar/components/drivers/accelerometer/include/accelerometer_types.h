#pragma once

#include <cstddef>
#include <cstdint>

namespace cownect::sensors {

// Raw output-register words (Material 5 section 8). Never replaced by converted g values.
struct AccelSample {
    int16_t x_raw;
    int16_t y_raw;
    int16_t z_raw;
};

struct AccelerometerCaptureStats {
    uint32_t samples_stored;
    uint32_t fifo_overrun_count;
    uint32_t i2c_error_count;
    uint32_t fifo_poll_count;
    uint32_t empty_poll_count;
    uint32_t discarded_startup_samples;
    uint32_t dropped_samples;
    uint32_t max_fifo_level_seen;

    uint64_t capture_start_us;
    uint64_t capture_end_us;
};

// Storage owned by the data layer / SensorManager; the driver only writes into it.
struct AccelCaptureBuffer {
    AccelSample* data;
    size_t capacity;
    size_t count;
};

}  // namespace cownect::sensors
