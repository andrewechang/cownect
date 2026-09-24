#pragma once

#include <cstdint>

// Device identity (Material 4 section 14). The exact provisioning source is not frozen:
// the current value is a configured DEVELOPMENT DEFAULT, clearly non-production.

uint64_t board_device_id();
bool board_device_id_is_development();
const char* board_firmware_version();
