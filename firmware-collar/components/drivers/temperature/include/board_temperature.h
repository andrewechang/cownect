#pragma once

#include "esp_err.h"
#include "slow_adc.h"
#include "temperature_types.h"

namespace cownect::sensors {

class IBoardTemperatureSensor {
public:
    virtual ~IBoardTemperatureSensor() = default;
    virtual esp_err_t init() = 0;
    virtual esp_err_t sample(BoardTemperatureSample& out) = 0;
};

// MCP9700A on GPIO1 / ADC1_CH0. Uses the nominal first-order transfer only.
// Does not own the clock, power rail, scheduler or communication.
class Mcp9700Driver final : public IBoardTemperatureSensor {
public:
    explicit Mcp9700Driver(IAdcSlowSampler& adc) : adc_(&adc) {}
    void set_adc(IAdcSlowSampler& adc) { adc_ = &adc; }

    esp_err_t init() override;
    // Fills everything except timestamp_ms (set by the caller from the shared capture clock).
    esp_err_t sample(BoardTemperatureSample& out) override;

    // Pure conversion of an already-acquired reading (shared with the integrated path).
    static void convert(const AdcReading& reading, BoardTemperatureSample& out);

private:
    IAdcSlowSampler* adc_;
};

}  // namespace cownect::sensors
