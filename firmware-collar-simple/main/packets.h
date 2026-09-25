// packets.h - byte formats sent to the gateway and the Jetson.
//
// All numbers are written byte by byte in little-endian order, so the receiver
// can decode them on any computer. The formats are the same as in the full
// firmware (docs/protocols.md), so the same gateway / Jetson code works.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "capture.h"
#include "summary.h"

// LoRa telemetry packet (49 bytes), magic "CM"
#define TELEMETRY_PACKET_BYTES 49
size_t packet_build_telemetry(const capture_t *c, const summary_t *s, uint8_t *out);

// Raw capture stream v1 (sent over Wi-Fi). It is sent in three parts so the
// 1.9 MB of microphone data never needs a second copy:
//   head  = stream header + accel + GPS + temperatures + microphone section header
//   mic   = the microphone samples straight from the capture buffer
//   tail  = empty diagnostics section header
#define STREAM_HEAD_MAX_BYTES  (68 + 12 * 5 + ACCEL_MAX_SAMPLES * 6 + GPS_MAX_FIXES * 24 + TEMP_MAX_SAMPLES * 44)
#define STREAM_TAIL_BYTES      12
uint32_t stream_total_bytes(const capture_t *c);
size_t stream_build_head(const capture_t *c, uint8_t *out);
size_t stream_build_tail(uint8_t *out);

// Copies n bytes starting at stream position `offset` into dst, taking them from
// the head buffer, the microphone buffer or the tail as needed (no full copy).
void stream_copy(const capture_t *c, const uint8_t *head, size_t head_len,
                 uint32_t offset, uint8_t *dst, size_t n);

// CRC-32 (zlib / Python binascii.crc32 variant). Start with crc = 0 and feed the
// data in any number of pieces: crc = crc32_update(crc, piece, len).
uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len);

// Header sent on the TCP connection before the stream (21 bytes), magic "CNUP"
#define UPLOAD_HEADER_BYTES 21
size_t packet_build_upload_header(const capture_t *c, uint32_t stream_bytes, uint8_t *out);
