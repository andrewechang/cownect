#pragma once

#include "accelerometer_types.h"
#include "esp_err.h"

namespace cownect::sensors {

// Public accelerometer API (Material 5 section 9). No communication, scheduling or sleep logic.
class IAccelerometer {
public:
    virtual ~IAccelerometer() = default;

    virtual esp_err_t init() = 0;
    virtual esp_err_t start_capture() = 0;
    virtual esp_err_t service() = 0;
    virtual esp_err_t stop_capture() = 0;

    virtual bool is_capturing() const = 0;
    virtual size_t sample_count() const = 0;

    virtual const AccelSample* samples() const = 0;
    virtual const AccelerometerCaptureStats& stats() const = 0;
};

}  // namespace cownect::sensors
