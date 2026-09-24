#pragma once

#include "analog_adc_types.h"
#include "esp_adc/adc_continuous.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace cownect::sensors {

struct MicSink {
    uint16_t* data;
    size_t capacity;
};

struct AnalogEngineConfig {
    const AnalogChannelId* pattern;  // scan order, e.g. MIC,BOARD_TEMP,MIC,COW_TEMP
    size_t pattern_len;
    uint32_t sample_freq_hz;          // TOTAL conversions per second
    MicSink mic;                      // may be {nullptr,0} if MIC is not in the pattern
    bool temperature_buckets;         // aggregate temperature channels into 1 s buckets
};

// Sole owner of ADC1 continuous mode (Material 9 section 7). Routes every conversion by its
// ADC metadata (never by position in the DMA frame). Does not convert temperatures and does
// no audio DSP. A dedicated high-priority task reads bounded frames; no logging in hot paths.
class AnalogAdcEngine {
public:
    esp_err_t start(const AnalogEngineConfig& cfg);
    // Stops conversion, processes already returned frames, releases ADC1. Idempotent.
    esp_err_t stop();
    bool running() const { return running_; }

    // Non-blocking; returns false if no completed bucket is queued.
    bool pop_bucket(AnalogBucketResult& out);

    const IntegratedAdcStats& stats() const { return stats_; }
    const MicStreamStats& mic_stats() const { return mic_; }
    uint64_t start_us() const { return start_us_; }
    uint64_t stop_us() const { return stop_us_; }

    // Test hook: artificial delay (ms) inserted after every frame to provoke pool overflow
    // (Material 8 test 8.4). Must be 0 in normal operation.
    void set_fault_injection_delay_ms(uint32_t ms) { fault_delay_ms_ = ms; }

private:
    struct Accumulator {
        uint64_t sum;
        uint32_t count;
        uint16_t min;
        uint16_t max;
        uint32_t near_low;
        uint32_t near_high;
        void reset() { *this = {0, 0, 0xFFFF, 0, 0, 0}; }
    };

    static void task_entry(void* arg);
    static bool on_pool_overflow(adc_continuous_handle_t handle, const adc_continuous_evt_data_t* edata, void* user);
    void task_loop();
    void process_frame(const uint8_t* data, uint32_t len);
    void route(uint32_t channel, uint16_t raw);
    void check_pattern(uint32_t channel);
    void close_buckets_until(uint32_t offset_ms);
    void emit_bucket(AnalogChannelId ch, Accumulator& acc, uint32_t bucket_index, uint32_t end_ms);
    void cleanup_handle();

    adc_continuous_handle_t handle_ = nullptr;
    TaskHandle_t task_ = nullptr;
    SemaphoreHandle_t task_done_ = nullptr;
    QueueHandle_t bucket_queue_ = nullptr;

    AnalogEngineConfig cfg_ = {};
    AnalogChannelId pattern_[8] = {};
    uint8_t pattern_raw_channel_[8] = {};
    uint8_t mic_channel_ = 0xFF;
    uint8_t board_channel_ = 0xFF;
    uint8_t cow_channel_ = 0xFF;
    size_t expected_idx_ = 0;
    bool pattern_synced_ = false;

    IntegratedAdcStats stats_ = {};
    MicStreamStats mic_ = {};
    Accumulator board_acc_ = {};
    Accumulator cow_acc_ = {};
    uint32_t next_bucket_index_ = 0;

    volatile bool stop_requested_ = false;
    volatile uint32_t pool_ovf_isr_count_ = 0;
    volatile uint32_t fault_delay_ms_ = 0;
    bool running_ = false;
    uint64_t start_us_ = 0;
    uint64_t stop_us_ = 0;

    uint8_t frame_[1024 * 4] = {};
};

AnalogAdcEngine& analog_engine();

}  // namespace cownect::sensors
