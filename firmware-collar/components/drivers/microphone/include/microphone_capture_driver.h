#pragma once

#include "analog_adc_engine.h"
#include "microphone.h"

namespace cownect::sensors {

// Two backends behind one IMicrophone API (Material 9 section 8):
//  - STANDALONE (Material 8): runs the shared ADC1 engine with a MIC-only pattern at 32 kS/s.
//  - INTEGRATED (Material 9): SensorManager runs the engine with MIC,TB,MIC,TC at 64 kS/s;
//    this driver only reports the microphone part of that stream.
class MicrophoneCaptureDriver final : public IMicrophone {
public:
    MicrophoneCaptureDriver(AnalogAdcEngine& engine, MicCaptureBuffer& buffer) : engine_(engine), buf_(buffer) {}

    void set_integrated_mode(bool integrated) { integrated_ = integrated; }

    esp_err_t init() override;
    esp_err_t start_capture() override;
    esp_err_t service() override;
    esp_err_t stop_capture() override;

    bool is_capturing() const override { return capturing_; }
    size_t sample_count() const override { return stats_.samples_stored; }
    const uint16_t* raw_samples() const override { return buf_.data; }
    const MicrophoneCaptureStats& stats() const override { return stats_; }

private:
    AnalogAdcEngine& engine_;
    MicCaptureBuffer& buf_;
    MicrophoneCaptureStats stats_ = {};
    bool integrated_ = false;
    bool initialized_ = false;
    bool capturing_ = false;
};

// Builds MicrophoneCaptureStats from the engine's raw stream counters (pure computation).
MicrophoneCaptureStats microphone_stats_from_engine(const MicStreamStats& mic, const IntegratedAdcStats& adc,
                                                    uint64_t start_us, uint64_t end_us);

}  // namespace cownect::sensors
