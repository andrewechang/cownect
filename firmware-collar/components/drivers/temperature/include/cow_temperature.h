#pragma once

#include "esp_err.h"
#include "slow_adc.h"
#include "temperature_math.h"
#include "temperature_types.h"

namespace cownect::sensors {

class ICowTemperatureSensor {
public:
    virtual ~ICowTemperatureSensor() = default;
    virtual esp_err_t init() = 0;
    virtual esp_err_t sample(CowTemperatureSample& out) = 0;
};

// External MF58-103F3950 on GPIO2 / ADC1_CH1 (removable JST probe).
// Open/short/near-rail/invalid-math states are reported explicitly; a disconnected probe never
// yields a plausible cattle temperature. Thresholds beyond rail/math checks are CONFIG_NOT_SET
// until bench-validated (NEEDS_HARDWARE_VALIDATION).
class Mf58Driver final : public ICowTemperatureSensor {
public:
    Mf58Driver(IAdcSlowSampler& adc, const IThermistorModel& model, const ThermistorElectricalConfig& electrical)
        : adc_(&adc), model_(&model), electrical_(electrical) {}
    void set_adc(IAdcSlowSampler& adc) { adc_ = &adc; }

    esp_err_t init() override;
    esp_err_t sample(CowTemperatureSample& out) override;

    void convert(const AdcReading& reading, CowTemperatureSample& out) const;

private:
    IAdcSlowSampler* adc_;
    const IThermistorModel* model_;
    ThermistorElectricalConfig electrical_;
};

// Default MF58 model and electrical configuration from central config (nominal excitation).
const IThermistorModel& mf58_default_model();
ThermistorElectricalConfig mf58_default_electrical();

}  // namespace cownect::sensors
