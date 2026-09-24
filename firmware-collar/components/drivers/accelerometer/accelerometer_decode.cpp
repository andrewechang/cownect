#include "accelerometer_decode.h"

#include <cmath>
#include "lis2dw12_regs.h"

namespace cownect::sensors {

FifoStatus decode_fifo_status(uint8_t reg)
{
    FifoStatus s;
    s.threshold_reached = (reg & lis2dw12::FIFO_FTH_FLAG_MASK) != 0;
    s.overrun = (reg & lis2dw12::FIFO_OVR_MASK) != 0;
    s.unread = static_cast<uint8_t>(reg & lis2dw12::FIFO_DIFF_MASK);
    return s;
}

static int16_t word_le(const uint8_t* p)
{
    return static_cast<int16_t>(static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8));
}

AccelSample decode_xyz(const uint8_t bytes[6])
{
    AccelSample s;
    s.x_raw = word_le(&bytes[0]);
    s.y_raw = word_le(&bytes[2]);
    s.z_raw = word_le(&bytes[4]);
    return s;
}

float accel_raw_to_g(int16_t raw_word)
{
    const int32_t digits = static_cast<int32_t>(raw_word) >> 2;  // arithmetic shift keeps sign
    return static_cast<float>(digits) * lis2dw12::SENSITIVITY_MG_PER_DIGIT_4G_HP / 1000.0f;
}

float accel_magnitude_g(const AccelSample& s)
{
    const float x = accel_raw_to_g(s.x_raw);
    const float y = accel_raw_to_g(s.y_raw);
    const float z = accel_raw_to_g(s.z_raw);
    return std::sqrt(x * x + y * y + z * z);
}

}  // namespace cownect::sensors
