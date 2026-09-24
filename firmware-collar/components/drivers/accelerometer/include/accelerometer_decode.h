#pragma once

#include <cstdint>
#include "accelerometer_types.h"

// Pure helpers, testable without hardware (Material 5 section 22).
namespace cownect::sensors {

struct FifoStatus {
    bool threshold_reached;
    bool overrun;
    uint8_t unread;
};

FifoStatus decode_fifo_status(uint8_t fifo_samples_reg);

// Little-endian OUT_X_L..OUT_Z_H -> signed words (two's complement).
AccelSample decode_xyz(const uint8_t bytes[6]);

// Engineering-unit conversion for +/-4 g High-Performance mode.
// The 14-bit result is left-justified in the 16-bit register pair, so the word is shifted right
// by 2 and multiplied by the nominal 0.488 mg/digit.
// NEEDS_HARDWARE_VALIDATION: Material 5 section 15 orientation test must confirm this scaling.
// If it is wrong, only this helper changes; retained raw samples are never modified.
float accel_raw_to_g(int16_t raw_word);

float accel_magnitude_g(const AccelSample& s);

}  // namespace cownect::sensors
