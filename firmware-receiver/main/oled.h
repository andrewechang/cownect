#pragma once

#include <stdbool.h>
#include <stdint.h>

bool oled_init(void);

void oled_show_status(uint32_t packet_count,
                      uint64_t collar_id,
                      uint32_t capture_id,
                      int rssi_dbm,
                      float snr_db,
                      bool has_packet);