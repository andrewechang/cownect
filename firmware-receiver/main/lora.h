#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool lora_config_ready(void);
bool lora_begin(void);

int lora_receive(uint8_t *buffer,
                 size_t buffer_size,
                 uint32_t timeout_ms);

int lora_last_packet_rssi(void);
float lora_last_packet_snr(void);

void lora_end(void);