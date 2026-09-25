// lora_full.h - LORA_FULL: send the whole raw capture over LoRa in fragments.
//
// The capture stream (~1.9 MB) is cut into fragments. Each fragment is one LoRa
// packet = 40-byte header + up to LORA_FULL_FRAGMENT_BYTES of stream data.
// With ACK on, the receiver must answer every fragment with a 21-byte FULL_ACK;
// a fragment without an ACK is re-sent up to LORA_FULL_MAX_RETRIES times, and
// the transfer stops if it still fails.
//
// Same packet format as the full firmware (docs/protocols.md section 4).
// NOTE: this is slow. At the fastest LoRa settings it still takes far longer
// than one 120 s cycle; the next capture simply starts late (cycle overrun).
// Used only when COMM_MODE = COMM_LORA_FULL (config.h). Radio settings: lora.h.
#pragma once
#include <stdbool.h>
#include "config.h"
#include "capture.h"

// ================================================================ SETTINGS
#define LORA_FULL_FRAGMENT_BYTES 0     // TODO stream bytes per packet, 1..215 (255 - 40 header) - 0 = not set
#define LORA_FULL_USE_ACK      1       // 1 = receiver answers every fragment with FULL_ACK, 0 = send and hope
#define LORA_FULL_ACK_TIMEOUT_MS 0     // TODO how long to wait for each ACK - 0 = not set
#define LORA_FULL_MAX_RETRIES  (-1)    // TODO re-sends per fragment before giving up (0 = send once) - -1 = not set
// ================================================================


// Returns true when the settings above are filled in and valid; otherwise logs why not.
bool lora_full_config_ready(void);

// Sends the whole capture stream. Returns true only if every fragment was sent
// (and, with ACK on, acknowledged). This is "complete" from the sender's side only.
bool lora_full_send(const capture_t *c);
