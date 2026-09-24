#include "config_parse.h"
#include "cownect_config.h"
#include "cownect_err.h"
#include "lora_full_transfer.h"
#include "sdkconfig.h"

namespace cownect::lorafull {

esp_err_t lora_full_load_config(LoraFullConfig& c, const char** missing)
{
    c = {};
    c.protocol_version = LORA_FULL_PROTOCOL_VERSION;
#if CONFIG_COWNECT_LORA_FULL_STREAM_CRC32
    c.stream_crc32_enabled = true;
#endif
    int64_t v = 0;
    if (!config::parse_int(CONFIG_COWNECT_LORA_FULL_FRAGMENT_PAYLOAD_BYTES, 1, 255, v)) {
        if (missing) *missing = "LORA_FULL_FRAGMENT_PAYLOAD_BYTES";
        return COWNECT_ERR_NOT_CONFIGURED;
    }
    if (DATA_HEADER_BYTES + static_cast<size_t>(v) > config::SX126X_MAX_PAYLOAD_BYTES) {
        if (missing) *missing = "LORA_FULL_FRAGMENT_PAYLOAD_BYTES (header + payload exceeds radio packet limit)";
        return ESP_ERR_INVALID_SIZE;
    }
    c.fragment_payload_bytes = static_cast<uint16_t>(v);
#if CONFIG_COWNECT_LORA_FULL_ACK_ENABLED
    c.reliability.ack_enabled = true;
    if (!config::parse_int(CONFIG_COWNECT_LORA_FULL_ACK_TIMEOUT_MS, 1, 262143, v)) {
        if (missing) *missing = "LORA_FULL_ACK_TIMEOUT_MS";
        return COWNECT_ERR_NOT_CONFIGURED;
    }
    c.reliability.ack_timeout_ms = static_cast<uint32_t>(v);
    if (!config::parse_int(CONFIG_COWNECT_LORA_FULL_MAX_RETRIES, 0, 255, v)) {
        if (missing) *missing = "LORA_FULL_MAX_RETRIES";
        return COWNECT_ERR_NOT_CONFIGURED;
    }
    c.reliability.max_retries_per_fragment = static_cast<uint8_t>(v);
#else
    c.reliability.ack_enabled = false;
    c.reliability.max_retries_per_fragment = 0;  // baseline A: send once
#endif
    if (missing) *missing = "";
    return ESP_OK;
}

}  // namespace cownect::lorafull
