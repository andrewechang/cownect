#pragma once

// Pure temperature conversion helpers (Material 6 sections 10-14, 25). No hardware access.

namespace cownect::sensors {

// Nominal MCP9700A first-order transfer: T = (mV - 500) / 10.
float mcp9700_mv_to_celsius(float millivolts);

// Divider: SWITCHED_3V3 -> R_fixed -> node -> R_ntc -> GND.  R_ntc = R_fixed * Vn / (Vexc - Vn).
// Returns false (no output) for Vn <= 0, Vn >= Vexc, non-positive/non-finite inputs.
bool ntc_divider_resistance(float node_mv, float excitation_mv, float fixed_resistor_ohm, float& resistance_ohm);

// Beta model: 1/T = 1/T25 + ln(R/R25)/B. Returns false for invalid inputs or non-finite result.
bool ntc_beta_resistance_to_celsius(float resistance_ohm, float r25_ohm, float beta_k, float& celsius);

// Replaceable thermistor model (Beta now; lookup/calibrated later) - Material 6 section 14.
class IThermistorModel {
public:
    virtual ~IThermistorModel() = default;
    virtual bool resistance_to_celsius(float resistance_ohm, float& celsius) const = 0;
    virtual const char* name() const = 0;
};

class BetaThermistorModel final : public IThermistorModel {
public:
    BetaThermistorModel(float r25_ohm, float beta_k) : r25_(r25_ohm), beta_(beta_k) {}
    bool resistance_to_celsius(float resistance_ohm, float& celsius) const override
    {
        return ntc_beta_resistance_to_celsius(resistance_ohm, r25_, beta_, celsius);
    }
    const char* name() const override { return "BETA"; }

private:
    float r25_;
    float beta_;
};

// Electrical assumptions isolated from the conversion (Material 6 section 12).
struct ThermistorElectricalConfig {
    float fixed_resistor_ohm;
    float excitation_mv;
    bool excitation_is_nominal;  // true: nominal SWITCHED_3V3, not a measured value
};

}  // namespace cownect::sensors
