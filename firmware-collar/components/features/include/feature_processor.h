#pragma once

#include <cstddef>
#include "capture_types.h"
#include "esp_err.h"
#include "telemetry_record.h"

namespace cownect::features {

// Material 11 section 5. Does not read CommunicationMode; never modifies the capture.
class IFeatureProcessor {
public:
    virtual ~IFeatureProcessor() = default;
    virtual esp_err_t process(const data::CaptureSession& capture, TelemetryRecord& out) = 0;
};

class SimpleFeatureProcessor final : public IFeatureProcessor {
public:
    esp_err_t process(const data::CaptureSession& capture, TelemetryRecord& out) override;
};

// Pure, unit-testable helpers (Material 11 section 20).
struct MagnitudeSummary {
    bool valid;
    float mean_g;
    float rms_g;
    float min_g;
    float max_g;
};
MagnitudeSummary summarize_accel_magnitude(const data::AccelSample* samples, size_t count);

// Index of the latest valid element, or -1.
int latest_valid_gps(const data::GpsFix* fixes, size_t count);
int latest_valid_board_temp(const data::BoardTemperatureSample* s, size_t count);
int latest_valid_cow_temp(const data::CowTemperatureSample* s, size_t count);

uint32_t telemetry_status_flags(const data::CaptureSession& capture);

}  // namespace cownect::features
