// lora_full.c - sends the whole capture stream over LoRa, fragment by fragment.
//
//   for each fragment:
//     build [40-byte header][payload] from the stream
//     send it; if ACK is on, wait for FULL_ACK with the same fragment number
//     no ACK -> send again (up to LORA_FULL_MAX_RETRIES), then give up
#include "lora_full.h"
#include <stdlib.h>
#include "config.h"
#include "capture.h"
#include "lora.h"
#include "packets.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "lora_full";

#define DATA_HEADER_BYTES 40
#define ACK_BYTES         21
#define MAGIC             0x4643   // "CF"
#define TYPE_DATA         1
#define TYPE_ACK          2
#define FLAG_CRC_PRESENT  0x0001
#define FLAG_LAST         0x0002

// put_u16/u32/u64: write a little-endian value at p, return the position after it.
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
// Reads a little-endian 32-bit value from p.
static uint32_t get_u32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

// Checks fragment size, ACK timeout and retries (see lora_full.h), and that
// header + fragment fits in one LoRa packet.
bool lora_full_config_ready(void)
{
    const char *missing = NULL;
    if (LORA_FULL_FRAGMENT_BYTES == 0) missing = "LORA_FULL_FRAGMENT_BYTES";
    else if (LORA_FULL_USE_ACK && LORA_FULL_ACK_TIMEOUT_MS == 0) missing = "LORA_FULL_ACK_TIMEOUT_MS";
    else if (LORA_FULL_USE_ACK && LORA_FULL_MAX_RETRIES < 0) missing = "LORA_FULL_MAX_RETRIES";
    if (missing) {
        ESP_LOGW(TAG, "LORA_FULL skipped: %s is not set in lora_full.h", missing);
        return false;
    }
    if (DATA_HEADER_BYTES + LORA_FULL_FRAGMENT_BYTES > LORA_MAX_PACKET) {
        ESP_LOGE(TAG, "LORA_FULL_FRAGMENT_BYTES too big (max %d)", LORA_MAX_PACKET - DATA_HEADER_BYTES);
        return false;
    }
    return true;
}

// Returns true if the received packet is a valid FULL_ACK (21 bytes) for this
// device, this capture and this fragment number, with status 0 (OK).
static bool is_my_ack(const uint8_t *a, int len, uint32_t capture_id, uint32_t index)
{
    if (len != ACK_BYTES) return false;
    uint64_t dev = 0;
    for (int i = 0; i < 8; i++) dev |= (uint64_t)a[4 + i] << (8 * i);
    return a[0] == (MAGIC & 0xFF) && a[1] == (MAGIC >> 8) && a[2] == 1 && a[3] == TYPE_ACK &&
           dev == DEVICE_ID && get_u32(&a[12]) == capture_id && get_u32(&a[16]) == index &&
           a[20] == 0;   // status 0 = OK
}

// 1. checks the settings, builds the stream head and computes the stream CRC-32
// 2. starts the radio
// 3. for each fragment: build header + payload, send, wait for ACK, retry if needed
// 4. stops the radio and logs COMPLETE or ABORTED with packet/retry counts
bool lora_full_send(const capture_t *c)
{
    if (!lora_config_ready() || !lora_full_config_ready()) return false;

    uint8_t *head = malloc(STREAM_HEAD_MAX_BYTES);
    if (!head) return false;
    size_t head_len = stream_build_head(c, head);
    uint32_t total = stream_total_bytes(c);
    const uint32_t frag = LORA_FULL_FRAGMENT_BYTES > 0 ? LORA_FULL_FRAGMENT_BYTES : 1;   // 0 = not set (checked above)
    uint32_t count = (total + frag - 1) / frag;

    // CRC-32 over the whole stream, so the receiver can check the rebuilt file.
    uint32_t crc = 0;
    uint8_t chunk[256];
    for (uint32_t off = 0; off < total; off += sizeof(chunk)) {
        size_t n = total - off < sizeof(chunk) ? total - off : sizeof(chunk);
        stream_copy(c, head, head_len, off, chunk, n);
        crc = crc32_update(crc, chunk, n);
    }

    ESP_LOGI(TAG, "capture %u: %u bytes -> %u fragments of %d bytes, ACK %s, CRC32 0x%08X",
             (unsigned)c->capture_id, (unsigned)total, (unsigned)count, (int)frag,
             LORA_FULL_USE_ACK ? "on" : "off", (unsigned)crc);

    if (!lora_begin()) {
        free(head);
        return false;
    }

    int64_t start = esp_timer_get_time();
    uint32_t retries = 0, sent = 0;
    bool ok = true;
    uint8_t pkt[LORA_MAX_PACKET], ack[LORA_MAX_PACKET];

    for (uint32_t idx = 0; idx < count && ok; idx++) {
        uint32_t offset = idx * frag;
        uint16_t payload = (uint16_t)(total - offset < frag ? total - offset : frag);

        // FULL_DATA header (40 bytes), then the payload
        uint8_t *p = pkt;
        p = put_u16(p, MAGIC);
        *p++ = 1;                                   // version
        *p++ = TYPE_DATA;
        p = put_u64(p, DEVICE_ID);
        p = put_u32(p, c->capture_id);
        p = put_u32(p, idx);
        p = put_u32(p, count);
        p = put_u32(p, offset);
        p = put_u32(p, total);
        p = put_u16(p, payload);
        p = put_u16(p, FLAG_CRC_PRESENT | (idx + 1 == count ? FLAG_LAST : 0));
        p = put_u32(p, crc);
        stream_copy(c, head, head_len, offset, p, payload);

        // Send, and with ACK on, wait for the matching ACK; retry if needed.
        int attempts = 0;
        bool delivered = false;
        while (!delivered) {
            attempts++;
            bool tx_ok = lora_transmit(pkt, DATA_HEADER_BYTES + payload);
            if (tx_ok) sent++;
            if (tx_ok && !LORA_FULL_USE_ACK) {
                delivered = true;
            } else if (tx_ok) {
                int n = lora_receive(ack, sizeof(ack), LORA_FULL_ACK_TIMEOUT_MS);
                delivered = n > 0 && is_my_ack(ack, n, c->capture_id, idx);
            }
            if (!delivered) {
                if (attempts > LORA_FULL_MAX_RETRIES) {
                    ESP_LOGE(TAG, "fragment %u failed after %d attempts - transfer stopped",
                             (unsigned)idx, attempts);
                    ok = false;
                    break;
                }
                retries++;
            }
        }

        if (ok && (idx + 1) % (count / 10 + 1) == 0) {
            ESP_LOGI(TAG, "progress %u%% (%u/%u fragments, %u retries)", (unsigned)((idx + 1) * 100 / count),
                     (unsigned)(idx + 1), (unsigned)count, (unsigned)retries);
        }
    }

    lora_end();
    free(head);
    ESP_LOGI(TAG, "%s: %u packets sent, %u retries, %lld s", ok ? "COMPLETE (sender side)" : "ABORTED",
             (unsigned)sent, (unsigned)retries, (esp_timer_get_time() - start) / 1000000);
    return ok;
}
