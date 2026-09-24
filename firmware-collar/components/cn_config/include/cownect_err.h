#pragma once

#include "esp_err.h"

// CowNect-specific error codes. Chosen outside the ESP-IDF error ranges.
#define COWNECT_ERR_BASE            0x7C00
#define COWNECT_ERR_NOT_CONFIGURED  (COWNECT_ERR_BASE + 1)  // CONFIG_NOT_SET: required value unresolved
#define COWNECT_ERR_TX_BLOCKED      (COWNECT_ERR_BASE + 2)  // RF TX gate (antenna/profile) not satisfied
#define COWNECT_ERR_BUSY_TIMEOUT    (COWNECT_ERR_BASE + 3)  // SX1262 BUSY did not clear in bound
#define COWNECT_ERR_RESOURCE_BUSY   (COWNECT_ERR_BASE + 4)  // e.g. ADC1 owned in an incompatible mode
#define COWNECT_ERR_WRONG_MODE      (COWNECT_ERR_BASE + 5)  // operation mode forbids the action
#define COWNECT_ERR_CAPTURE_FROZEN  (COWNECT_ERR_BASE + 6)  // CaptureSession still owned by a transfer
#define COWNECT_ERR_INCOMPLETE      (COWNECT_ERR_BASE + 7)  // transfer ended without full completion

static inline const char* cownect_err_name(esp_err_t err)
{
    switch (err) {
    case COWNECT_ERR_NOT_CONFIGURED: return "CONFIG_NOT_SET";
    case COWNECT_ERR_TX_BLOCKED:     return "RF_TX_BLOCKED";
    case COWNECT_ERR_BUSY_TIMEOUT:   return "BUSY_TIMEOUT";
    case COWNECT_ERR_RESOURCE_BUSY:  return "RESOURCE_BUSY";
    case COWNECT_ERR_WRONG_MODE:     return "WRONG_OPERATION_MODE";
    case COWNECT_ERR_CAPTURE_FROZEN: return "CAPTURE_FROZEN_FOR_TRANSFER";
    case COWNECT_ERR_INCOMPLETE:     return "INCOMPLETE";
    default:                         return esp_err_to_name(err);
    }
}
