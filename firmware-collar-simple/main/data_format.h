// data_format.h - the byte formats sent to the gateway and the Jetson.
//
// Every number is written byte by byte, little-endian, so any computer can decode
// it. The layouts match the full firmware (firmware-collar/docs/protocols.md);
// this version only adds the MIC_CLIPPED flag bits (see capture.h).
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "capture.h"
#include "features.h"

// 49-byte LoRa telemetry packet, magic "CM"
#define TELEMETRY_BYTES 49
size_t build_telemetry(const capture_t *c, const summary_t *s, uint8_t *out);

// Feature packet (COMM_LORA_FULL), magic "CM", type 0x11, one LoRa packet, little-endian:
//    0  "CM"            2   version 2, type 0x11
//    4  device u64     12   capture u32
//   16  valid u8        bit0 GPS, 1 board temp, 2 cow temp, 3 accel, 4 mic (as telemetry)
//   17  flags u32       SUM_* bits (capture.h)
//   21  lat i32, 25 lon i32   degrees x 1e7 (0 without a fix)
//   29  GPS satellites u8, 30 GPS fix quality u8
//   31  board temp i16, 33 cow temp i16   C x 100 (-32768 = not valid)
//   35  capture ms u16, 37 block ms u16, 39 block count u8 (B), 40 second count u8 (S)
//   41  loud sound events u8
//   42  B blocks x 20 bytes: mean X/Y/Z i16 mg, ODBA u16 mg, peak u16 mg, moves u8,
//                            sound low/mid/high u8 (dB x 3; 0 also = no samples),
//                            jerk X/Y/Z u16 (average |da/dt|, g/s x 100)
//       then S bytes: loudness per second (dB x 3)
// 15 s with 3 s blocks = 42 + 5 x 20 + 15 = 157 bytes.
#define FEATURES_VERSION      2
#define FEATURES_HEADER_BYTES 42
#define FEATURES_BLOCK_BYTES  20
#define FEATURES_MAX_BYTES    (FEATURES_HEADER_BYTES + FEATURE_MAX_BLOCKS * FEATURES_BLOCK_BYTES + FEATURE_MAX_SECONDS)
size_t build_features(const capture_t *c, const summary_t *s, const features_t *f, uint8_t *out);

// Raw recording ("capture stream v1", COMM_HYBRID Wi-Fi upload). To avoid a second 1 MB
// copy it is made of:
//   head = 68-byte header + accel, GPS, temperature sections + microphone section header
//   mic  = the microphone samples, straight from the capture buffer
//   tail = an empty 12-byte diagnostics section header
#define STREAM_HEAD_MAX (68 + 6 * 12 + ACCEL_MAX_SAMPLES * 6 + GPS_MAX_FIXES * 24 + TEMP_MAX_SAMPLES * 44)
#define STREAM_TAIL_BYTES 12
uint32_t stream_size(const capture_t *c);
size_t build_stream_head(const capture_t *c, uint8_t *out);
size_t build_stream_tail(uint8_t *out);

// 21-byte header sent before the stream on the Wi-Fi TCP connection, magic "CNUP"
#define UPLOAD_HEADER_BYTES 21
size_t build_upload_header(const capture_t *c, uint32_t stream_bytes, uint8_t *out);
