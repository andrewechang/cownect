// data_format.h - the byte formats sent to the gateway and the Jetson.
//
// Every number is written byte by byte, little-endian, so any computer can decode
// it. The layouts match the full firmware (firmware-collar/docs/protocols.md);
// this version only adds the MIC_CLIPPED flag bits (see capture.h).
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "capture.h"

// 49-byte LoRa telemetry packet, magic "CM"
#define TELEMETRY_BYTES 49
size_t build_telemetry(const capture_t *c, const summary_t *s, uint8_t *out);

// Raw recording ("capture stream v1"). To avoid a second 1.9 MB copy it is made of:
//   head = 68-byte header + accel, GPS, temperature sections + microphone section header
//   mic  = the microphone samples, straight from the capture buffer
//   tail = an empty 12-byte diagnostics section header
#define STREAM_HEAD_MAX (68 + 5 * 12 + ACCEL_MAX_SAMPLES * 6 + GPS_MAX_FIXES * 24 + TEMP_MAX_SAMPLES * 44)
#define STREAM_TAIL_BYTES 12
uint32_t stream_size(const capture_t *c);
size_t build_stream_head(const capture_t *c, uint8_t *out);
size_t build_stream_tail(uint8_t *out);

// Copies n stream bytes starting at `offset` (from head, mic buffer or tail as needed).
void stream_copy(const capture_t *c, const uint8_t *head, size_t head_len, uint32_t offset, uint8_t *dst, size_t n);

// Running CRC-32 (zlib variant). Start with 0 and feed the data in pieces.
uint32_t crc32_add(uint32_t crc, const uint8_t *data, size_t len);

// 21-byte header sent before the stream on the Wi-Fi TCP connection, magic "CNUP"
#define UPLOAD_HEADER_BYTES 21
size_t build_upload_header(const capture_t *c, uint32_t stream_bytes, uint8_t *out);
