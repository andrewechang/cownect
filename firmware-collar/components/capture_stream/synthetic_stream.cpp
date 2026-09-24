#include "synthetic_stream.h"

namespace cownect::codec {

esp_err_t SyntheticStream::read(uint32_t offset, uint8_t* dst, size_t cap, size_t& written) const
{
    written = 0;
    if (offset > total_ || dst == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    while (written < cap && offset < total_) {
        dst[written++] = byte_at(offset++, seed_);
    }
    return ESP_OK;
}

}  // namespace cownect::codec
