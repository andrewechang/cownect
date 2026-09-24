#include "atgm336h_driver.h"

#include <cstring>
#include "board_pins.h"
#include "board_uart.h"
#include "cownect_config.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"

namespace cownect::sensors {
namespace {
constexpr const char* TAG = "GPS";
constexpr int kUartQueueLength = 20;
constexpr size_t kReadChunk = 256;
constexpr size_t kMaxBytesPerService = 2048;  // bounds one service() call
}  // namespace

esp_err_t Atgm336hDriver::init()
{
    if (!installed_) {
        uart_config_t cfg = {};
        cfg.baud_rate = static_cast<int>(config::GPS_BAUD);
        cfg.data_bits = UART_DATA_8_BITS;
        cfg.parity = UART_PARITY_DISABLE;
        cfg.stop_bits = UART_STOP_BITS_1;
        cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;  // no flow-control pins on the PCB
        cfg.source_clk = UART_SCLK_DEFAULT;
        esp_err_t err = uart_driver_install(board::GPS_UART, static_cast<int>(config::GPS_UART_RX_BUFFER_BYTES), 0,
                                            kUartQueueLength, &uart_queue_, 0);
        if (err != ESP_OK) {
            return err;
        }
        err = uart_param_config(board::GPS_UART, &cfg);
        if (err == ESP_OK) {
            // ESP TX -> GPS RXD (GPIO18), ESP RX <- GPS TXD (GPIO17)
            err = uart_set_pin(board::GPS_UART, board::PIN_GPS_UART_TX, board::PIN_GPS_UART_RX, UART_PIN_NO_CHANGE,
                               UART_PIN_NO_CHANGE);
        }
        if (err != ESP_OK) {
            uart_driver_delete(board::GPS_UART);
            uart_queue_ = nullptr;
            return err;
        }
        installed_ = true;
        ESP_LOGI(TAG, "UART%d %u 8N1 RX=GPIO%d TX=GPIO%d", static_cast<int>(board::GPS_UART),
                 static_cast<unsigned>(config::GPS_BAUD), board::PIN_GPS_UART_RX, board::PIN_GPS_UART_TX);
    }
    framer_.reset();
    builder_.reset();
    return ESP_OK;
}

esp_err_t Atgm336hDriver::deinit()
{
    capturing_ = false;
    if (!installed_) {
        return ESP_OK;
    }
    installed_ = false;
    uart_queue_ = nullptr;
    return uart_driver_delete(board::GPS_UART);
}

void Atgm336hDriver::reset_statistics()
{
    stats_ = {};
    sentence_stat_count_ = 0;
    std::memset(sentence_stats_, 0, sizeof(sentence_stats_));
}

esp_err_t Atgm336hDriver::start_capture()
{
    if (!installed_) {
        return ESP_ERR_INVALID_STATE;
    }
    if (buf_.data == nullptr || buf_.capacity == 0) {
        return ESP_ERR_NO_MEM;
    }
    buf_.count = 0;
    reset_statistics();
    builder_.reset();
    // The UART RX buffer is not flushed: a coherent stream from an already powered module is kept.
    stats_.capture_start_us = static_cast<uint64_t>(esp_timer_get_time());
    capturing_ = true;
    ESP_LOGI(TAG, "capture start");
    return ESP_OK;
}

uint32_t Atgm336hDriver::offset_ms_now() const
{
    const int64_t now = esp_timer_get_time();
    const int64_t start = static_cast<int64_t>(stats_.capture_start_us);
    return now > start ? static_cast<uint32_t>((now - start) / 1000) : 0;
}

void Atgm336hDriver::handle_uart_events()
{
    uart_event_t ev;
    while (uart_queue_ != nullptr && xQueueReceive(uart_queue_, &ev, 0) == pdTRUE) {
        if (ev.type == UART_FIFO_OVF || ev.type == UART_BUFFER_FULL) {
            stats_.uart_overflow_count++;
            uart_flush_input(board::GPS_UART);
            xQueueReset(uart_queue_);
            framer_.reset();  // byte continuity lost
            break;
        }
    }
}

void Atgm336hDriver::record_identifier(const char* body, bool checksum_ok)
{
    char id[sizeof(GpsSentenceStat::identifier)] = {};
    size_t i = 0;
    while (body[i] != '\0' && body[i] != ',' && i < sizeof(id) - 1) {
        id[i] = body[i];
        ++i;
    }
    for (size_t k = 0; k < sentence_stat_count_; ++k) {
        if (std::strcmp(sentence_stats_[k].identifier, id) == 0) {
            sentence_stats_[k].count++;
            if (checksum_ok) sentence_stats_[k].checksum_ok++;
            return;
        }
    }
    if (sentence_stat_count_ < kMaxIdentifiers) {
        GpsSentenceStat& s = sentence_stats_[sentence_stat_count_++];
        std::memcpy(s.identifier, id, sizeof(id));
        s.count = 1;
        s.checksum_ok = checksum_ok ? 1 : 0;
    }
}

void Atgm336hDriver::store_fix(const GpsFix& f, bool duplicate)
{
    if (duplicate) {
        stats_.duplicate_epoch_count++;
        return;
    }
    if (!f.valid) {
        stats_.invalid_fix_count++;
        return;
    }
    stats_.valid_fix_count++;
    if (!capturing_) {
        return;
    }
    if (buf_.count < buf_.capacity) {
        buf_.data[buf_.count++] = f;
    } else {
        stats_.dropped_fix_count++;  // never overwrite earlier fixes
    }
}

void Atgm336hDriver::handle_sentence(const char* body, size_t len, bool checksum_present)
{
    stats_.framed_sentence_count++;
    record_identifier(body, checksum_present);
    if (observer_ != nullptr) {
        observer_(body, observer_ctx_);
    }
    GpsFixUpdate u = {};
    esp_err_t err = parser_.consume_sentence(body, len, &u);
    if (err == ESP_ERR_NOT_SUPPORTED) {
        stats_.unsupported_sentence_count++;
        return;
    }
    if (err != ESP_OK) {
        stats_.parse_error_count++;
        return;
    }
    GpsFix f = {};
    bool dup = false;
    switch (builder_.feed(u, offset_ms_now(), f, dup)) {
    case GpsFixBuilder::FeedResult::EMITTED:
        store_fix(f, dup);
        break;
    case GpsFixBuilder::FeedResult::UNATTRIBUTED:
        stats_.invalid_fix_count++;  // no time field: cannot form an epoch, no coordinate stored
        break;
    case GpsFixBuilder::FeedResult::MERGED:
        break;
    }
}

void Atgm336hDriver::handle_byte(char c)
{
    switch (framer_.push(c)) {
    case NmeaFramer::Result::SENTENCE:
        handle_sentence(framer_.body(), framer_.body_length(), framer_.checksum_present());
        break;
    case NmeaFramer::Result::CHECKSUM_ERROR:
        stats_.checksum_error_count++;
        break;
    case NmeaFramer::Result::OVERLENGTH:
        stats_.overlength_count++;
        break;
    case NmeaFramer::Result::NONE:
        break;
    }
}

esp_err_t Atgm336hDriver::service()
{
    if (!installed_) {
        return ESP_ERR_INVALID_STATE;
    }
    handle_uart_events();
    uint8_t chunk[kReadChunk];
    size_t total = 0;
    while (total < kMaxBytesPerService) {
        size_t available = 0;
        if (uart_get_buffered_data_len(board::GPS_UART, &available) != ESP_OK || available == 0) {
            break;
        }
        const size_t want = available < sizeof(chunk) ? available : sizeof(chunk);
        const int n = uart_read_bytes(board::GPS_UART, chunk, want, 0);
        if (n <= 0) {
            break;
        }
        stats_.uart_bytes_received += static_cast<uint32_t>(n);
        stats_.last_byte_us = static_cast<uint64_t>(esp_timer_get_time());
        for (int i = 0; i < n; ++i) {
            handle_byte(static_cast<char>(chunk[i]));
        }
        total += static_cast<size_t>(n);
    }
    return ESP_OK;
}

esp_err_t Atgm336hDriver::stop_capture()
{
    if (!capturing_) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = service();  // bounded drain of already-received bytes
    GpsFix f = {};
    bool dup = false;
    if (builder_.flush(f, dup)) {
        store_fix(f, dup);
    }
    stats_.capture_end_us = static_cast<uint64_t>(esp_timer_get_time());
    capturing_ = false;
    ESP_LOGI(TAG, "capture stop fixes=%u sentences=%u checksum_err=%u parse_err=%u uart_ovf=%u dropped=%u",
             static_cast<unsigned>(buf_.count), static_cast<unsigned>(stats_.framed_sentence_count),
             static_cast<unsigned>(stats_.checksum_error_count), static_cast<unsigned>(stats_.parse_error_count),
             static_cast<unsigned>(stats_.uart_overflow_count), static_cast<unsigned>(stats_.dropped_fix_count));
    return err;
}

}  // namespace cownect::sensors
