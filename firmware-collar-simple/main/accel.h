// accel.h - LIS2DW12 accelerometer (I2C). Keeps its own data in accel_data_t.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "config.h"

// ================================================================ SETTINGS
#define ACCEL_I2C_ADDR         0x18    // LIS2DW12 address on this PCB
#define ACCEL_I2C_HZ           400000  // I2C clock
#define ACCEL_RATE_HZ          50      // 25, 50, 100 or 200 Hz (baseline 50)
#define ACCEL_RANGE_G          4       // 2, 4, 8 or 16 g       (baseline 4)
#define ACCEL_READ_EVERY_MS    100     // how often the FIFO is emptied
// ================================================================

// Register values for the settings above (from the LIS2DW12 datasheet).
#if   ACCEL_RATE_HZ == 25
  #define ACCEL_RATE_BITS 0x30
#elif ACCEL_RATE_HZ == 50
  #define ACCEL_RATE_BITS 0x40
#elif ACCEL_RATE_HZ == 100
  #define ACCEL_RATE_BITS 0x50
#elif ACCEL_RATE_HZ == 200
  #define ACCEL_RATE_BITS 0x60
#else
  #error "ACCEL_RATE_HZ must be 25, 50, 100 or 200"
#endif

#if   ACCEL_RANGE_G == 2
  #define ACCEL_RANGE_BITS 0x00
  #define ACCEL_MG_PER_DIGIT 0.244f
#elif ACCEL_RANGE_G == 4
  #define ACCEL_RANGE_BITS 0x10
  #define ACCEL_MG_PER_DIGIT 0.488f
#elif ACCEL_RANGE_G == 8
  #define ACCEL_RANGE_BITS 0x20
  #define ACCEL_MG_PER_DIGIT 0.976f
#elif ACCEL_RANGE_G == 16
  #define ACCEL_RANGE_BITS 0x30
  #define ACCEL_MG_PER_DIGIT 1.952f
#else
  #error "ACCEL_RANGE_G must be 2, 4, 8 or 16"
#endif

#if ACCEL_READ_EVERY_MS * ACCEL_RATE_HZ >= 32 * 1000
  #error "ACCEL_READ_EVERY_MS too long: the 32-sample FIFO would overflow"
#endif

#define ACCEL_MAX_SAMPLES      (ACCEL_RATE_HZ * (CAPTURE_MS / 1000 + 2))   // capture + 2 s margin

typedef struct {
    int16_t x, y, z;           // raw sensor words (14-bit value, left-justified)
} accel_sample_t;

typedef struct {
    accel_sample_t samples[ACCEL_MAX_SAMPLES];
    uint32_t count;
    uint32_t overruns;         // PROBLEM CHECK: FIFO overflowed -> samples were lost
    bool ok;                   // sensor found and configured
} accel_data_t;

// Starts I2C, checks the chip ID and starts the sensor. False if it does not answer.
bool accel_start(void);

// Moves every sample waiting in the sensor's FIFO into a. Call every ACCEL_READ_EVERY_MS.
void accel_read_fifo(accel_data_t *a);

// Powers the sensor down and releases I2C.
void accel_stop(void);
