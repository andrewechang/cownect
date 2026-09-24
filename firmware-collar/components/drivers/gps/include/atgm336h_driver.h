#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "gps.h"
#include "gps_fix_builder.h"
#include "nmea_framer.h"
#include "nmea_parser.h"

namespace cownect::sensors {

struct GpsFixBuffer {
    GpsFix* data;
    size_t capacity;
    size_t count;
};

// ATGM336H-5N31: ESP RX GPIO17 <- GPS TXD, ESP TX GPIO18 -> GPS RXD, 9600 8N1, NMEA.
// The module default (9600 baud, 1 Hz) already matches the baseline, so no configuration
// command is ever sent over UART TX.
class Atgm336hDriver final : public IGps {
public:
    explicit Atgm336hDriver(GpsFixBuffer& buffer) : buf_(buffer) {}

    esp_err_t init() override;
    esp_err_t start_capture() override;
    esp_err_t service() override;
    esp_err_t stop_capture() override;
    esp_err_t deinit();

    bool is_capturing() const override { return capturing_; }
    size_t fix_count() const override { return buf_.count; }
    const GpsFix* fixes() const override { return buf_.data; }
    const GpsCaptureStats& stats() const override { return stats_; }

    // Development helpers (sentence discovery / bounded NMEA printing).
    const GpsSentenceStat* sentence_stats(size_t& count) const
    {
        count = sentence_stat_count_;
        return sentence_stats_;
    }
    using SentenceObserver = void (*)(const char* body, void* ctx);
    void set_sentence_observer(SentenceObserver obs, void* ctx)
    {
        observer_ = obs;
        observer_ctx_ = ctx;
    }
    // Resets statistics and discovery tables without starting fix retention.
    void reset_statistics();

private:
    void handle_uart_events();
    void handle_byte(char c);
    void handle_sentence(const char* body, size_t len, bool checksum_present);
    void record_identifier(const char* body, bool checksum_ok);
    void store_fix(const GpsFix& f, bool duplicate);
    uint32_t offset_ms_now() const;

    GpsFixBuffer& buf_;
    GpsCaptureStats stats_ = {};
    NmeaFramer framer_;
    StandardNmeaParser parser_;
    GpsFixBuilder builder_;
    QueueHandle_t uart_queue_ = nullptr;
    bool installed_ = false;
    bool capturing_ = false;

    static constexpr size_t kMaxIdentifiers = 16;
    GpsSentenceStat sentence_stats_[kMaxIdentifiers] = {};
    size_t sentence_stat_count_ = 0;

    SentenceObserver observer_ = nullptr;
    void* observer_ctx_ = nullptr;
};

}  // namespace cownect::sensors
