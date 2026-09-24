#pragma once

#include "esp_err.h"
#include "gps_types.h"

namespace cownect::sensors {

// Public GPS API (Material 7 section 17). Never blocks waiting for a fix, never touches the
// power rail, radios, sleep or the system clock.
class IGps {
public:
    virtual ~IGps() = default;

    virtual esp_err_t init() = 0;
    virtual esp_err_t start_capture() = 0;
    virtual esp_err_t service() = 0;  // bounded: drains what is available, returns
    virtual esp_err_t stop_capture() = 0;

    virtual bool is_capturing() const = 0;
    virtual size_t fix_count() const = 0;

    virtual const GpsFix* fixes() const = 0;
    virtual const GpsCaptureStats& stats() const = 0;
};

}  // namespace cownect::sensors
