#pragma once

#include "esp_err.h"
#include "microphone_types.h"

namespace cownect::sensors {

// Public microphone API (Material 8 section 12). Analog path only (not I2S, not PDM).
class IMicrophone {
public:
    virtual ~IMicrophone() = default;

    virtual esp_err_t init() = 0;
    virtual esp_err_t start_capture() = 0;
    virtual esp_err_t service() = 0;
    virtual esp_err_t stop_capture() = 0;

    virtual bool is_capturing() const = 0;
    virtual size_t sample_count() const = 0;
    virtual const uint16_t* raw_samples() const = 0;
    virtual const MicrophoneCaptureStats& stats() const = 0;
};

}  // namespace cownect::sensors
