// accel.h - LIS2DW12 accelerometer over I2C.
#pragma once
#include <stdbool.h>
#include "config.h"

// ================================================================ SETTINGS
#define ACCEL_I2C_ADDR         0x18    // LIS2DW12 address on this PCB (SA0 = 0)
#define ACCEL_I2C_HZ           400000  // I2C clock

#define ACCEL_ODR_HZ           50      // sample rate: 25, 50, 100 or 200 Hz  (baseline 50)
#define ACCEL_RANGE_G          4       // full scale: 2, 4, 8 or 16 g          (baseline 4)
#define ACCEL_POLL_MS          100     // how often the FIFO is emptied during the capture
// ================================================================

// Derived values - no need to edit below this line.
#if   ACCEL_ODR_HZ == 25
  #define ACCEL_ODR_BITS 0x30
#elif ACCEL_ODR_HZ == 50
  #define ACCEL_ODR_BITS 0x40
#elif ACCEL_ODR_HZ == 100
  #define ACCEL_ODR_BITS 0x50
#elif ACCEL_ODR_HZ == 200
  #define ACCEL_ODR_BITS 0x60
#else
  #error "ACCEL_ODR_HZ must be 25, 50, 100 or 200"
#endif

// FS bits for CTRL6 and the sensitivity in high-performance (14-bit) mode, from the datasheet
#if   ACCEL_RANGE_G == 2
  #define ACCEL_FS_BITS 0x00
  #define ACCEL_MG_PER_DIGIT 0.244f
#elif ACCEL_RANGE_G == 4
  #define ACCEL_FS_BITS 0x10
  #define ACCEL_MG_PER_DIGIT 0.488f
#elif ACCEL_RANGE_G == 8
  #define ACCEL_FS_BITS 0x20
  #define ACCEL_MG_PER_DIGIT 0.976f
#elif ACCEL_RANGE_G == 16
  #define ACCEL_FS_BITS 0x30
  #define ACCEL_MG_PER_DIGIT 1.952f
#else
  #error "ACCEL_RANGE_G must be 2, 4, 8 or 16"
#endif

// The FIFO holds 32 samples; it must be emptied before it fills up.
#if ACCEL_POLL_MS * ACCEL_ODR_HZ >= 32 * 1000
  #error "ACCEL_POLL_MS is too long for this ACCEL_ODR_HZ (FIFO would overflow)"
#endif

// Room for the whole capture plus 2 s of margin
#define ACCEL_MAX_SAMPLES      (ACCEL_ODR_HZ * (CAPTURE_MS / 1000 + 2))

typedef struct capture_s capture_t;

// Creates the I2C bus, checks the chip ID, sets rate/range and starts the FIFO.
// Returns false if the sensor does not answer or cannot be configured.
bool accel_start(void);

// Reads every sample waiting in the FIFO and appends it to the capture.
// Call it about every ACCEL_POLL_MS.
void accel_poll(capture_t *c);

// Puts the sensor in power-down and releases the I2C bus.
void accel_stop(void);
