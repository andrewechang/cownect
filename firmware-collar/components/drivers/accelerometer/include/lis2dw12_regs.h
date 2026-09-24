#pragma once

#include <cstdint>

// LIS2DW12 registers and fields used by CowNect (datasheet facts listed in Material 5 section 4).
namespace cownect::sensors::lis2dw12 {

inline constexpr uint8_t REG_WHO_AM_I     = 0x0F;
inline constexpr uint8_t REG_CTRL1        = 0x20;
inline constexpr uint8_t REG_CTRL2        = 0x21;
inline constexpr uint8_t REG_CTRL6        = 0x25;
inline constexpr uint8_t REG_STATUS       = 0x27;
inline constexpr uint8_t REG_OUT_X_L      = 0x28;
inline constexpr uint8_t REG_FIFO_CTRL    = 0x2E;
inline constexpr uint8_t REG_FIFO_SAMPLES = 0x2F;

// CTRL1: ODR[7:4] MODE[3:2] LP_MODE[1:0]
inline constexpr uint8_t CTRL1_ODR_MASK      = 0xF0;
inline constexpr uint8_t CTRL1_ODR_POWERDOWN = 0x00;
inline constexpr uint8_t CTRL1_ODR_50HZ      = 0x40;  // 0100
inline constexpr uint8_t CTRL1_MODE_MASK     = 0x0C;
inline constexpr uint8_t CTRL1_MODE_HP       = 0x04;  // 01 High-Performance, 14-bit
inline constexpr uint8_t CTRL1_LPMODE_MASK   = 0x03;
inline constexpr uint8_t CTRL1_LPMODE_00     = 0x00;

// CTRL2
inline constexpr uint8_t CTRL2_BDU         = 0x08;
inline constexpr uint8_t CTRL2_IF_ADD_INC  = 0x04;
inline constexpr uint8_t CTRL2_I2C_DISABLE = 0x02;

// CTRL6: BW_FILT[7:6] FS[5:4] FDS[3] LOW_NOISE[2]
inline constexpr uint8_t CTRL6_BW_MASK  = 0xC0;
inline constexpr uint8_t CTRL6_BW_ODR2  = 0x00;
inline constexpr uint8_t CTRL6_FS_MASK  = 0x30;
inline constexpr uint8_t CTRL6_FS_4G    = 0x10;
inline constexpr uint8_t CTRL6_FDS      = 0x08;

// FIFO_CTRL: FMode[7:5] FTH[4:0]
inline constexpr uint8_t FIFO_MODE_MASK       = 0xE0;
inline constexpr uint8_t FIFO_MODE_BYPASS     = 0x00;
inline constexpr uint8_t FIFO_MODE_CONTINUOUS = 0xC0;  // 110
inline constexpr uint8_t FIFO_FTH_MASK        = 0x1F;

// FIFO_SAMPLES
inline constexpr uint8_t FIFO_FTH_FLAG_MASK = 0x80;
inline constexpr uint8_t FIFO_OVR_MASK      = 0x40;
inline constexpr uint8_t FIFO_DIFF_MASK     = 0x3F;

inline constexpr uint8_t FIFO_DEPTH = 32;
inline constexpr uint8_t BYTES_PER_SAMPLE = 6;

// Nominal sensitivity for +/-4 g High-Performance (14-bit) mode.
inline constexpr float SENSITIVITY_MG_PER_DIGIT_4G_HP = 0.488f;

}  // namespace cownect::sensors::lis2dw12
