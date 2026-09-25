// packets.c - writes the packets field by field (see packets.h and docs/protocols.md).
#include "packets.h"
#include <string.h>

// put_u8/u16/u32/u64/f32: write one value at p in little-endian byte order and
// return the position just after it, so fields can be written one after another.
static uint8_t *put_u8(uint8_t *p, uint8_t v)   { *p++ = v; return p; }
static uint8_t *put_u16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; return p + 2; }
static uint8_t *put_u32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
    return p + 4;
}
static uint8_t *put_u64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i));
    return p + 8;
}
static uint8_t *put_f32(uint8_t *p, float v)
{
    uint32_t bits;
    memcpy(&bits, &v, 4);                 // IEEE-754 float bits
    return put_u32(p, bits);
}

// ------------------------------------------------------------------ telemetry
// Writes the 49-byte LoRa telemetry packet into out: magic, version, type,
// device/capture ID, which values are valid, then the values. Invalid values are sent as 0.
size_t packet_build_telemetry(const capture_t *c, const summary_t *s, uint8_t *out)
{
    uint8_t valid = (s->gps_valid << 0) | (s->board_valid << 1) | (s->cow_valid << 2) |
                    (s->accel_valid << 3) | (s->mic_valid << 4);
    uint8_t *p = out;
    p = put_u16(p, 0x4D43);               // magic "CM"
    p = put_u8(p, 1);                     // version
    p = put_u8(p, 0x10);                  // type: telemetry
    p = put_u64(p, DEVICE_ID);
    p = put_u32(p, c->capture_id);
    p = put_u8(p, valid);
    p = put_u32(p, (uint32_t)(s->gps_valid ? s->lat_e7 : 0));
    p = put_u32(p, (uint32_t)(s->gps_valid ? s->lon_e7 : 0));
    p = put_f32(p, s->board_valid ? s->board_temp_c : 0);
    p = put_f32(p, s->cow_valid ? s->cow_temp_c : 0);
    p = put_f32(p, s->accel_valid ? s->accel_mean_g : 0);
    p = put_f32(p, s->accel_valid ? s->accel_rms_g : 0);
    p = put_f32(p, s->mic_valid ? s->mic_rms : 0);
    p = put_u32(p, s->status_flags);
    return (size_t)(p - out);             // = TELEMETRY_PACKET_BYTES
}

// ------------------------------------------------------------------ raw stream
enum { SEC_ACCEL = 1, SEC_GPS = 2, SEC_BOARD_TEMP = 3, SEC_COW_TEMP = 4, SEC_MIC = 5, SEC_DIAG = 6 };

// Writes a 12-byte section header: type, version, record size, record count, total bytes.
static uint8_t *put_section(uint8_t *p, uint8_t type, uint16_t record_bytes, uint32_t count)
{
    p = put_u8(p, type);
    p = put_u8(p, 1);                     // section version
    p = put_u16(p, record_bytes);
    p = put_u32(p, count);
    return put_u32(p, record_bytes * count);
}

// Size of the complete stream: header + 6 section headers + all records.
uint32_t stream_total_bytes(const capture_t *c)
{
    return 68 + 12 * 6 + c->accel_count * 6 + c->gps_count * 24 + c->board_temp_count * 20 +
           c->cow_temp_count * 24 + c->mic_count * 2;
}

// Status code stored with each temperature record: 0 OK, 2 no calibration, 3 invalid voltage.
static uint8_t temp_status(const temp_sample_t *t)
{
    if (t->mv < 0) return 2;              // CAL_UNAVAILABLE
    return t->valid ? 0 : 3;              // OK / INVALID_VOLTAGE
}

// Writes the first part of the stream into out: the 68-byte header, the accel,
// GPS, board and cow temperature sections, and the microphone section header.
// Returns the number of bytes written (at most STREAM_HEAD_MAX_BYTES).
size_t stream_build_head(const capture_t *c, uint8_t *out)
{
    uint8_t *p = out;
    // Stream header (68 bytes)
    p = put_u32(p, 0x53434E43);           // magic "CNCS"
    p = put_u8(p, 1);                     // version
    p = put_u8(p, 0);
    p = put_u16(p, 68);                   // header size
    p = put_u64(p, DEVICE_ID);
    p = put_u32(p, c->capture_id);
    p = put_u64(p, (uint64_t)c->start_us);
    p = put_u64(p, (uint64_t)c->end_us);
    p = put_u32(p, stream_total_bytes(c));
    p = put_u32(p, c->accel_count);
    p = put_u32(p, c->gps_count);
    p = put_u32(p, c->board_temp_count);
    p = put_u32(p, c->cow_temp_count);
    p = put_u32(p, c->mic_count);
    p = put_u32(p, MIC_SAMPLE_RATE_HZ);
    p = put_u32(p, capture_status_flags(c));

    p = put_section(p, SEC_ACCEL, 6, c->accel_count);
    for (uint32_t i = 0; i < c->accel_count; i++) {
        p = put_u16(p, (uint16_t)c->accel[i].x);
        p = put_u16(p, (uint16_t)c->accel[i].y);
        p = put_u16(p, (uint16_t)c->accel[i].z);
    }

    p = put_section(p, SEC_GPS, 24, c->gps_count);
    for (uint32_t i = 0; i < c->gps_count; i++) {
        const gps_fix_t *g = &c->gps[i];
        p = put_u32(p, g->offset_ms);
        p = put_u32(p, (uint32_t)g->lat_e7);
        p = put_u32(p, (uint32_t)g->lon_e7);
        p = put_f32(p, g->speed_mps);
        p = put_f32(p, g->course_deg);
        p = put_u8(p, g->fix_quality);
        p = put_u8(p, g->satellites);
        p = put_u8(p, g->valid ? 1 : 0); // flags: bit0 valid
        p = put_u8(p, 0);
    }

    p = put_section(p, SEC_BOARD_TEMP, 20, c->board_temp_count);
    for (uint32_t i = 0; i < c->board_temp_count; i++) {
        const temp_sample_t *t = &c->board_temp[i];
        p = put_u32(p, t->ts_ms);
        p = put_u32(p, (uint32_t)t->adc_raw);
        p = put_u32(p, (uint32_t)t->mv);
        p = put_f32(p, t->temp_c);
        p = put_u8(p, temp_status(t));
        p = put_u8(p, t->mv >= 0 ? 1 : 0); // flags: bit0 millivolts valid
        p = put_u16(p, 0);
    }

    p = put_section(p, SEC_COW_TEMP, 24, c->cow_temp_count);
    for (uint32_t i = 0; i < c->cow_temp_count; i++) {
        const temp_sample_t *t = &c->cow_temp[i];
        p = put_u32(p, t->ts_ms);
        p = put_u32(p, (uint32_t)t->adc_raw);
        p = put_u32(p, (uint32_t)t->mv);
        p = put_f32(p, t->resistance_ohm);
        p = put_f32(p, t->temp_c);
        p = put_u8(p, temp_status(t));
        p = put_u8(p, t->mv >= 0 ? 1 : 0);
        p = put_u16(p, 0);
    }

    // Only the section header here; the samples follow straight from c->mic.
    p = put_section(p, SEC_MIC, 2, c->mic_count);
    return (size_t)(p - out);
}

// Writes the last 12 bytes of the stream: an empty diagnostics section header.
size_t stream_build_tail(uint8_t *out)
{
    // The simple firmware sends no diagnostics record (count 0); receivers skip it.
    uint8_t *p = put_section(out, SEC_DIAG, 144, 0);
    return (size_t)(p - out);
}

// Gives the bytes of the stream at any position, without building the whole
// stream in memory: positions in the head come from `head`, the next part from
// the microphone buffer, and the last 12 bytes from the tail.
void stream_copy(const capture_t *c, const uint8_t *head, size_t head_len,
                 uint32_t offset, uint8_t *dst, size_t n)
{
    const uint8_t *mic = (const uint8_t *)c->mic;     // little-endian on the ESP32 = stream byte order
    size_t mic_len = c->mic_count * sizeof(uint16_t);
    uint8_t tail[STREAM_TAIL_BYTES];
    stream_build_tail(tail);

    for (size_t i = 0; i < n; i++) {
        size_t pos = offset + i;
        if (pos < head_len) dst[i] = head[pos];
        else if (pos < head_len + mic_len) dst[i] = mic[pos - head_len];
        else dst[i] = tail[pos - head_len - mic_len];
    }
}

// Adds `len` bytes to a running CRC-32 (zlib variant, check value for
// "123456789" = 0xCBF43926). The lookup table is built on the first call.
uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len)
{
    static uint32_t table[256];
    if (table[1] == 0) {                                  // build the lookup table once
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t v = i;
            for (int k = 0; k < 8; k++) v = (v & 1) ? (v >> 1) ^ 0xEDB88320u : v >> 1;
            table[i] = v;
        }
    }
    crc = ~crc;
    for (size_t i = 0; i < len; i++) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

// Writes the 21-byte header sent before the stream on the TCP connection:
// magic "CNUP", version, device ID, capture ID, stream size.
size_t packet_build_upload_header(const capture_t *c, uint32_t stream_bytes, uint8_t *out)
{
    uint8_t *p = out;
    p = put_u32(p, 0x50554E43);           // magic "CNUP"
    p = put_u8(p, 1);                     // version
    p = put_u64(p, DEVICE_ID);
    p = put_u32(p, c->capture_id);
    p = put_u32(p, stream_bytes);
    return (size_t)(p - out);             // = UPLOAD_HEADER_BYTES
}
