#include "prototype_tcp_client.h"

#include <cerrno>
#include <cstring>
#include "esp_log.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

// Explicit lwip_* calls are used below; drop any BSD-name compatibility macros so they cannot
// rewrite this class's member names (connect/close).
#ifdef connect
#undef connect
#endif
#ifdef close
#undef close
#endif
#ifdef send
#undef send
#endif

namespace cownect::tcp {
namespace {
constexpr const char* TAG = "TCP";

timeval to_timeval(uint32_t ms)
{
    timeval tv = {};
    tv.tv_sec = static_cast<long>(ms / 1000);
    tv.tv_usec = static_cast<long>((ms % 1000) * 1000);
    return tv;
}
}  // namespace

void PrototypeTcpClient::reset_counters()
{
    bytes_sent_ = 0;
    write_calls_ = 0;
    socket_errors_ = 0;
}

esp_err_t PrototypeTcpClient::connect(const char* ip, uint16_t port, uint32_t timeout_ms)
{
    close();
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (ip == nullptr || inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
        return ESP_ERR_INVALID_ARG;
    }
    sock_ = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (sock_ < 0) {
        socket_errors_++;
        return ESP_FAIL;
    }
    // Non-blocking connect bounded by select().
    const int flags = lwip_fcntl(sock_, F_GETFL, 0);
    lwip_fcntl(sock_, F_SETFL, flags | O_NONBLOCK);
    int rc = lwip_connect(sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (rc != 0 && errno != EINPROGRESS) {
        socket_errors_++;
        ESP_LOGW(TAG, "CONNECT_FAILED errno=%d", errno);
        close();
        return ESP_FAIL;
    }
    if (rc != 0) {
        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(sock_, &wfds);
        timeval tv = to_timeval(timeout_ms);
        rc = lwip_select(sock_ + 1, nullptr, &wfds, nullptr, &tv);
        if (rc <= 0) {
            ESP_LOGW(TAG, "CONNECT_TIMEOUT");
            socket_errors_++;
            close();
            return ESP_ERR_TIMEOUT;
        }
        int so_error = 0;
        socklen_t len = sizeof(so_error);
        lwip_getsockopt(sock_, SOL_SOCKET, SO_ERROR, &so_error, &len);
        if (so_error != 0) {
            ESP_LOGW(TAG, "CONNECT_FAILED so_error=%d", so_error);
            socket_errors_++;
            close();
            return ESP_FAIL;
        }
    }
    lwip_fcntl(sock_, F_SETFL, flags & ~O_NONBLOCK);
    current_timeout_ms_ = 0;
    ESP_LOGI(TAG, "connected %s:%u", ip, static_cast<unsigned>(port));
    return ESP_OK;
}

esp_err_t PrototypeTcpClient::send_all(const uint8_t* data, size_t length, uint32_t timeout_ms)
{
    if (sock_ < 0) {
        return ESP_ERR_INVALID_STATE;
    }
    if (timeout_ms != current_timeout_ms_) {
        timeval tv = to_timeval(timeout_ms);
        lwip_setsockopt(sock_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        current_timeout_ms_ = timeout_ms;
    }
    size_t sent = 0;
    while (sent < length) {
        write_calls_++;
        const int n = lwip_send(sock_, data + sent, length - sent, 0);
        if (n > 0) {
            sent += static_cast<size_t>(n);  // advance only by bytes actually accepted
            bytes_sent_ += static_cast<uint64_t>(n);
            continue;
        }
        socket_errors_++;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            ESP_LOGW(TAG, "WRITE_TIMEOUT sent=%llu", static_cast<unsigned long long>(bytes_sent_));
            return ESP_ERR_TIMEOUT;
        }
        ESP_LOGW(TAG, "WRITE_FAILED errno=%d sent=%llu", errno, static_cast<unsigned long long>(bytes_sent_));
        return ESP_FAIL;
    }
    return ESP_OK;
}

void PrototypeTcpClient::close()
{
    if (sock_ >= 0) {
        lwip_shutdown(sock_, SHUT_RDWR);
        lwip_close(sock_);
        sock_ = -1;
    }
}

}  // namespace cownect::tcp
