#pragma once

#include <cstddef>
#include <cstdint>
#include "esp_err.h"

namespace cownect::tcp {

// Simple prototype TCP client (Material 14 Rev B section 9). No TLS, no framework.
class PrototypeTcpClient {
public:
    ~PrototypeTcpClient() { close(); }

    esp_err_t connect(const char* ip, uint16_t port, uint32_t timeout_ms);
    // Sends every byte, handling partial writes. Each write may block up to timeout_ms; a stalled
    // socket returns ESP_ERR_TIMEOUT. bytes_sent() is accurate on failure.
    esp_err_t send_all(const uint8_t* data, size_t length, uint32_t timeout_ms);
    void close();
    bool connected() const { return sock_ >= 0; }

    uint64_t bytes_sent() const { return bytes_sent_; }
    uint32_t write_calls() const { return write_calls_; }
    uint32_t socket_errors() const { return socket_errors_; }
    void reset_counters();

private:
    int sock_ = -1;
    uint64_t bytes_sent_ = 0;
    uint32_t write_calls_ = 0;
    uint32_t socket_errors_ = 0;
    uint32_t current_timeout_ms_ = 0;
};

}  // namespace cownect::tcp
