// features.h - COMM_LORA_FULL: turns one recording into a few numbers that fit in ONE
// LoRa packet (157 bytes for 15 s), instead of sending the raw samples.
//
// The recording is cut into blocks of FEATURE_BLOCK_MS. Per block:
//   accelerometer: average X/Y/Z (tilt = posture), ODBA (movement level), largest jolt,
//                  number of movement bursts, and the derivative (jerk) of X, Y and Z:
//                  average |change between two samples| / time between them, in g/s
//   microphone:    loudness in three frequency bands (low / mid / high)
// Per second:      microphone loudness
// Whole recording: number of short loud sound events, GPS, temperatures, problem flags.
//
// Packet layout: data_format.c build_features(). Used only when COMM_MODE = COMM_LORA_FULL.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "config.h"
#include "capture.h"

// ================================================================ SETTINGS
#define FEATURE_BLOCK_MS       3000    // block length: 15 s recording = 5 blocks
// Microphone bands (Hz). Chewing / rumination is mostly LOW, calls (moo) LOW + MID,
// rubbing / scraping and wind noise mostly HIGH.
#define FEATURE_BAND_LOW_HZ    50,  400
#define FEATURE_BAND_MID_HZ    400, 2000
#define FEATURE_BAND_HIGH_HZ   2000, 8000
// Movement = acceleration minus the block average (gravity). A movement burst starts when its
// size rises above FEATURE_MOVE_START_MG and ends when it drops below FEATURE_MOVE_END_MG.
#define FEATURE_MOVE_START_MG  150
#define FEATURE_MOVE_END_MG    75
// A loud sound event = a 100 ms window at least FEATURE_EVENT_DB above the recording's
// typical (median) loudness. Neighbouring loud windows count as one event.
#define FEATURE_EVENT_DB       10.0f
// ================================================================

// Upper limits, so the packet always fits in one LoRa packet (255 bytes):
// 9 blocks x 20 bytes + 30 seconds x 1 byte + 42 header bytes = 252 bytes.
#define FEATURE_MAX_BLOCKS     9
#define FEATURE_MAX_SECONDS    30

typedef struct {
    bool accel_ok;                  // this block has accelerometer samples
    int16_t mean_mg[3];             // average X, Y, Z in mg (shows posture / collar tilt)
    uint16_t odba_mg;               // average |x-mean|+|y-mean|+|z-mean| in mg = movement level
    uint16_t peak_mg;               // largest movement (acceleration minus gravity) in mg = biggest jolt
    uint8_t moves;                  // movement bursts
    uint16_t jerk_cgs[3];           // derivative of X, Y, Z: average |da/dt| in g/s x 100
    bool mic_ok;                   // this block has microphone samples
    uint8_t band[3];                // low / mid / high band loudness, dB x 3 (code explained below)
} feature_block_t;

typedef struct {
    uint16_t capture_ms;            // real length of the recording
    uint8_t block_count;
    feature_block_t blocks[FEATURE_MAX_BLOCKS];
    uint8_t second_count;
    uint8_t loudness[FEATURE_MAX_SECONDS];   // microphone loudness per second, dB x 3
    uint8_t sound_events;           // short loud sounds in the whole recording
} features_t;

// Loudness code: 3 x 20*log10(RMS in ADC codes), 0..255. RMS 1 -> 0, RMS 10 -> 60, RMS 1000 -> 180.
// Back to RMS: 10^(code / 60). One step = 0.33 dB.

// Computes all features from the recording (takes ~0.8 s for 15 s of audio on the collar).
void features_compute(const capture_t *c, features_t *f);
