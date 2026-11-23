// data_format.c - writes the packets field by field (layouts in data_format.h).
#include "data_format.h"
#include <math.h>
#include <string.h>

// put_*: write one value at p (little-endian) and return the position after it.
static uint8_t *put_u8(uint8_t *p, uint8_t v)   { p[0] = v; return p + 1; }
static uint8_t *put_u16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; return p + 2; }
static uint8_t *put_u32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = v >> (8 * i); return p + 4; }
static uint8_t *put_u64(uint8_t *p, uint64_t v) { for (int i = 0; i < 8; i++) p[i] = v >> (8 * i); return p + 8; }
static uint8_t *put_f32(uint8_t *p, float v)    { uint32_t b; memcpy(&b, &v, 4); return put_u32(p, b); }

// Start of both LoRa packets: "CM", version, type, device ID, capture ID, which values are valid
// (bit0 GPS, 1 board temp, 2 cow temp, 3 accel, 4 mic).
static uint8_t *put_lora_header(uint8_t *p, uint8_t version, uint8_t type, const capture_t *c, const summary_t *s)
{
    p = put_u16(p, 0x4D43);                     // "CM"
    p = put_u8(p, version);
    p = put_u8(p, type);
    p = put_u64(p, DEVICE_ID);
    p = put_u32(p, c->capture_id);
    return put_u8(p, s->gps_valid | (s->board_valid << 1) | (s->cow_valid << 2) |
                     (s->accel_valid << 3) | (s->mic_valid << 4));
}

// 49-byte telemetry: header, the values (0 if invalid), flags.
size_t build_telemetry(const capture_t *c, const summary_t *s, uint8_t *out)
{
    uint8_t *p = put_lora_header(out, 1, 0x10, c, s);   // version 1, type telemetry
    p = put_u32(p, s->gps_valid ? (uint32_t)s->lat_e7 : 0);
    p = put_u32(p, s->gps_valid ? (uint32_t)s->lon_e7 : 0);
    p = put_f32(p, s->board_valid ? s->board_temp_c : 0);
    p = put_f32(p, s->cow_valid ? s->cow_temp_c : 0);
    p = put_f32(p, s->accel_valid ? s->accel_mean_g : 0);
    p = put_f32(p, s->accel_valid ? s->accel_rms_g : 0);
    p = put_f32(p, s->mic_valid ? s->mic_rms : 0);
    p = put_u32(p, s->flags);
    return p - out;
}

// Temperature as C x 100 in an i16; -32768 when not valid.
static uint16_t temp_x100(bool valid, float c)
{
    if (!valid || isnan(c)) return 0x8000;
    float v = c * 100.0f;
    if (v > 32767) v = 32767;
    if (v < -32767) v = -32767;
    return (uint16_t)(int16_t)lroundf(v);
}

// Feature packet, layout in data_format.h.
size_t build_features(const capture_t *c, const summary_t *s, const features_t *f, uint8_t *out)
{
    const gps_fix_t *last = c->gps.count ? &c->gps.fixes[c->gps.count - 1] : NULL;  // latest GGA
    uint8_t *p = put_lora_header(out, FEATURES_VERSION, 0x11, c, s);                // type features
    p = put_u32(p, s->flags);
    p = put_u32(p, s->gps_valid ? (uint32_t)s->lat_e7 : 0);
    p = put_u32(p, s->gps_valid ? (uint32_t)s->lon_e7 : 0);
    p = put_u8(p, last ? last->satellites : 0);
    p = put_u8(p, last ? last->fix_quality : 0);
    p = put_u16(p, temp_x100(s->board_valid, s->board_temp_c));
    p = put_u16(p, temp_x100(s->cow_valid, s->cow_temp_c));
    p = put_u16(p, f->capture_ms);
    p = put_u16(p, FEATURE_BLOCK_MS);
    p = put_u8(p, f->block_count);
    p = put_u8(p, f->second_count);
    p = put_u8(p, f->sound_events);
    for (uint32_t b = 0; b < f->block_count; b++) {
        const feature_block_t *k = &f->blocks[b];
        for (int a = 0; a < 3; a++) p = put_u16(p, (uint16_t)k->mean_mg[a]);
        p = put_u16(p, k->odba_mg);
        p = put_u16(p, k->peak_mg);
        p = put_u8(p, k->moves);
        for (int a = 0; a < 3; a++) p = put_u8(p, k->band[a]);
        for (int a = 0; a < 3; a++) p = put_u16(p, k->jerk_cgs[a]);
    }
    for (uint32_t i = 0; i < f->second_count; i++) p = put_u8(p, f->loudness[i]);
    return p - out;
}

// 12-byte section header: type, version, bytes per record, record count, total bytes.
static uint8_t *put_section(uint8_t *p, uint8_t type, uint16_t record_bytes, uint32_t count)
{
    p = put_u8(p, type);
    p = put_u8(p, 1);
    p = put_u16(p, record_bytes);
    p = put_u32(p, count);
    return put_u32(p, record_bytes * count);
}

// Total stream size: header + 6 section headers + all records.
uint32_t stream_size(const capture_t *c)
{
    return 68 + 6 * 12 + c->accel.count * 6 + c->gps.count * 24 + c->analog.board_count * 20 +
           c->analog.cow_count * 24 + c->analog.mic_count * 2;
}

// Header + accel, GPS, board, cow sections + microphone section header.
size_t build_stream_head(const capture_t *c, uint8_t *out)
{
    const analog_data_t *a = &c->analog;
    uint8_t *p = out;
    p = put_u32(p, 0x53434E43);                 // "CNCS"
    p = put_u8(p, 1);
    p = put_u8(p, 0);
    p = put_u16(p, 68);
    p = put_u64(p, DEVICE_ID);
    p = put_u32(p, c->capture_id);
    p = put_u64(p, c->start_us);
    p = put_u64(p, c->end_us);
    p = put_u32(p, stream_size(c));
    p = put_u32(p, c->accel.count);
    p = put_u32(p, c->gps.count);
    p = put_u32(p, a->board_count);
    p = put_u32(p, a->cow_count);
    p = put_u32(p, a->mic_count);
    p = put_u32(p, MIC_SAMPLE_RATE_HZ);
    p = put_u32(p, capture_flags(c));

    p = put_section(p, 1, 6, c->accel.count);                        // 1 = accelerometer
    for (uint32_t i = 0; i < c->accel.count; i++) {
        p = put_u16(p, c->accel.samples[i].x);
        p = put_u16(p, c->accel.samples[i].y);
        p = put_u16(p, c->accel.samples[i].z);
    }

    p = put_section(p, 2, 24, c->gps.count);                         // 2 = GPS
    for (uint32_t i = 0; i < c->gps.count; i++) {
        const gps_fix_t *g = &c->gps.fixes[i];
        p = put_u32(p, g->time_ms);
        p = put_u32(p, g->lat_e7);
        p = put_u32(p, g->lon_e7);
        p = put_f32(p, g->speed_mps);
        p = put_f32(p, g->course_deg);
        p = put_u8(p, g->fix_quality);
        p = put_u8(p, g->satellites);
        p = put_u8(p, g->valid);
        p = put_u8(p, 0);
    }

    p = put_section(p, 3, 20, a->board_count);                       // 3 = board temperature
    for (uint32_t i = 0; i < a->board_count; i++) {
        const temp_sample_t *t = &a->board[i];
        p = put_u32(p, t->time_ms);
        p = put_u32(p, t->adc_raw);
        p = put_u32(p, t->mv);
        p = put_f32(p, t->temp_c);
        p = put_u8(p, t->status);              // temp_status_t (analog.h)
        p = put_u8(p, t->mv >= 0);
        p = put_u16(p, 0);
    }

    p = put_section(p, 4, 24, a->cow_count);                         // 4 = cow temperature
    for (uint32_t i = 0; i < a->cow_count; i++) {
        const temp_sample_t *t = &a->cow[i];
        p = put_u32(p, t->time_ms);
        p = put_u32(p, t->adc_raw);
        p = put_u32(p, t->mv);
        p = put_f32(p, t->resistance_ohm);
        p = put_f32(p, t->temp_c);
        p = put_u8(p, t->status);              // temp_status_t (analog.h)
        p = put_u8(p, t->mv >= 0);
        p = put_u16(p, 0);
    }

    p = put_section(p, 5, 2, a->mic_count);                          // 5 = microphone (samples follow)
    return p - out;
}

// Empty diagnostics section (type 6, no records); receivers skip it.
size_t build_stream_tail(uint8_t *out)
{
    return put_section(out, 6, 144, 0) - out;
}

// 21 bytes: "CNUP", version, device ID, capture ID, stream size.
size_t build_upload_header(const capture_t *c, uint32_t stream_bytes, uint8_t *out)
{
    uint8_t *p = out;
    p = put_u32(p, 0x50554E43);
    p = put_u8(p, 1);
    p = put_u64(p, DEVICE_ID);
    p = put_u32(p, c->capture_id);
    p = put_u32(p, stream_bytes);
    return p - out;
}
