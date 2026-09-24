#include "analog_adc_engine.h"

#include <cstring>
#include "board_adc.h"
#include "cownect_config.h"
#include "cownect_err.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "hal/adc_types.h"
#include "soc/soc_caps.h"

namespace cownect::sensors {
namespace {
constexpr const char* TAG = "ADC_ENGINE";
constexpr UBaseType_t kTaskPriority = configMAX_PRIORITIES - 3;
constexpr uint32_t kTaskStack = 4096;
constexpr BaseType_t kTaskCore = 1;
constexpr uint32_t kTaskStopWaitMs = 1000;
constexpr UBaseType_t kBucketQueueLength = 8;

board::AnalogInput to_input(AnalogChannelId ch)
{
    switch (ch) {
    case AnalogChannelId::MIC:        return board::AnalogInput::MIC;
    case AnalogChannelId::BOARD_TEMP: return board::AnalogInput::BOARD_TEMP;
    case AnalogChannelId::COW_TEMP:   return board::AnalogInput::COW_TEMP;
    }
    return board::AnalogInput::MIC;
}
}  // namespace

bool IRAM_ATTR AnalogAdcEngine::on_pool_overflow(adc_continuous_handle_t, const adc_continuous_evt_data_t*, void* user)
{
    static_cast<AnalogAdcEngine*>(user)->pool_ovf_isr_count_ = static_cast<AnalogAdcEngine*>(user)->pool_ovf_isr_count_ + 1;
    return false;
}

esp_err_t AnalogAdcEngine::start(const AnalogEngineConfig& cfg)
{
    if (running_) {
        return ESP_ERR_INVALID_STATE;
    }
    if (cfg.pattern == nullptr || cfg.pattern_len == 0 || cfg.pattern_len > sizeof(pattern_) / sizeof(pattern_[0]) ||
        cfg.pattern_len > SOC_ADC_PATT_LEN_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config::ADC_CONV_FRAME_BYTES % SOC_ADC_DIGI_RESULT_BYTES != 0 || config::ADC_CONV_FRAME_BYTES > sizeof(frame_)) {
        ESP_LOGE(TAG, "conv frame bytes %u invalid", static_cast<unsigned>(config::ADC_CONV_FRAME_BYTES));
        return ESP_ERR_INVALID_ARG;
    }
    cfg_ = cfg;
    stats_ = {};
    mic_ = {};
    mic_.raw_min = 0xFFFF;
    board_acc_.reset();
    cow_acc_.reset();
    next_bucket_index_ = 0;
    expected_idx_ = 0;
    pattern_synced_ = false;
    pool_ovf_isr_count_ = 0;
    stop_requested_ = false;
    mic_channel_ = board_channel_ = cow_channel_ = 0xFF;

    adc_digi_pattern_config_t patt[SOC_ADC_PATT_LEN_MAX] = {};
    bool has_mic = false;
    for (size_t i = 0; i < cfg.pattern_len; ++i) {
        board::AdcChannelMap map;
        esp_err_t err = board::adc_channel_for(to_input(cfg.pattern[i]), map);
        if (err != ESP_OK) {
            return err;
        }
        pattern_[i] = cfg.pattern[i];
        pattern_raw_channel_[i] = static_cast<uint8_t>(map.channel);
        patt[i].atten = static_cast<uint8_t>(board::adc_attenuation());  // one common attenuation
        patt[i].channel = static_cast<uint8_t>(map.channel);
        patt[i].unit = static_cast<uint8_t>(map.unit);
        patt[i].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;
        switch (cfg.pattern[i]) {
        case AnalogChannelId::MIC:        mic_channel_ = patt[i].channel; has_mic = true; break;
        case AnalogChannelId::BOARD_TEMP: board_channel_ = patt[i].channel; break;
        case AnalogChannelId::COW_TEMP:   cow_channel_ = patt[i].channel; break;
        }
    }
    if (has_mic && (cfg.mic.data == nullptr || cfg.mic.capacity == 0)) {
        return ESP_ERR_NO_MEM;
    }
    stats_.sample_freq_hz = cfg.sample_freq_hz;
    stats_.pattern_len = static_cast<uint8_t>(cfg.pattern_len);

    esp_err_t err = board::adc_resource().acquire(board::AdcMode::CONTINUOUS);
    if (err != ESP_OK) {
        return err;
    }
    if (task_done_ == nullptr) {
        task_done_ = xSemaphoreCreateBinary();
    }
    if (bucket_queue_ == nullptr) {
        bucket_queue_ = xQueueCreate(kBucketQueueLength, sizeof(AnalogBucketResult));
    }
    if (task_done_ == nullptr || bucket_queue_ == nullptr) {
        board::adc_resource().release(board::AdcMode::CONTINUOUS);
        return ESP_ERR_NO_MEM;
    }
    xQueueReset(bucket_queue_);
    xSemaphoreTake(task_done_, 0);

    adc_continuous_handle_cfg_t hcfg = {};
    hcfg.max_store_buf_size = config::ADC_POOL_BYTES;
    hcfg.conv_frame_size = config::ADC_CONV_FRAME_BYTES;
    err = adc_continuous_new_handle(&hcfg, &handle_);
    if (err != ESP_OK) {
        handle_ = nullptr;
        board::adc_resource().release(board::AdcMode::CONTINUOUS);
        return err;
    }
    adc_continuous_config_t ccfg = {};
    ccfg.pattern_num = static_cast<uint32_t>(cfg.pattern_len);
    ccfg.adc_pattern = patt;
    ccfg.sample_freq_hz = cfg.sample_freq_hz;
    ccfg.conv_mode = ADC_CONV_SINGLE_UNIT_1;
    ccfg.format = ADC_DIGI_OUTPUT_FORMAT_TYPE2;
    err = adc_continuous_config(handle_, &ccfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_continuous_config rejected pattern_len=%u fs=%u: %s", static_cast<unsigned>(cfg.pattern_len),
                 static_cast<unsigned>(cfg.sample_freq_hz), esp_err_to_name(err));
        cleanup_handle();
        return err;
    }
    adc_continuous_evt_cbs_t cbs = {};
    cbs.on_pool_ovf = &AnalogAdcEngine::on_pool_overflow;
    err = adc_continuous_register_event_callbacks(handle_, &cbs, this);
    if (err != ESP_OK) {
        cleanup_handle();
        return err;
    }
    if (xTaskCreatePinnedToCore(&AnalogAdcEngine::task_entry, "analog_adc", kTaskStack, this, kTaskPriority, &task_,
                                kTaskCore) != pdPASS) {
        task_ = nullptr;
        cleanup_handle();
        return ESP_ERR_NO_MEM;
    }
    start_us_ = static_cast<uint64_t>(esp_timer_get_time());
    err = adc_continuous_start(handle_);
    if (err != ESP_OK) {
        stop_requested_ = true;
        xSemaphoreTake(task_done_, pdMS_TO_TICKS(kTaskStopWaitMs));
        task_ = nullptr;
        cleanup_handle();
        return err;
    }
    running_ = true;
    ESP_LOGI(TAG, "start fs_total=%u pattern_len=%u atten=%s (PROVISIONAL)", static_cast<unsigned>(cfg.sample_freq_hz),
             static_cast<unsigned>(cfg.pattern_len), board::adc_attenuation_name());
    return ESP_OK;
}

void AnalogAdcEngine::cleanup_handle()
{
    if (handle_ != nullptr) {
        adc_continuous_deinit(handle_);
        handle_ = nullptr;
    }
    board::adc_resource().release(board::AdcMode::CONTINUOUS);
}

void AnalogAdcEngine::task_entry(void* arg)
{
    static_cast<AnalogAdcEngine*>(arg)->task_loop();
}

void AnalogAdcEngine::task_loop()
{
    while (!stop_requested_) {
        uint32_t len = 0;
        esp_err_t err = adc_continuous_read(handle_, frame_, config::ADC_CONV_FRAME_BYTES, &len, config::ADC_READ_TIMEOUT_MS);
        if (err == ESP_OK) {
            process_frame(frame_, len);
            if (fault_delay_ms_ > 0) {
                vTaskDelay(pdMS_TO_TICKS(fault_delay_ms_));
            }
        } else if (err == ESP_ERR_TIMEOUT) {
            stats_.read_timeout_count++;
        } else {
            stats_.driver_error_count++;
            vTaskDelay(1);  // never spin on a persistent driver error
        }
    }
    xSemaphoreGive(task_done_);
    vTaskDelete(nullptr);
}

void AnalogAdcEngine::check_pattern(uint32_t channel)
{
    if (stats_.observed_count < sizeof(stats_.observed_first_channels)) {
        stats_.observed_first_channels[stats_.observed_count++] = static_cast<uint8_t>(channel);
    }
    const size_t n = cfg_.pattern_len;
    if (pattern_synced_ && pattern_raw_channel_[expected_idx_] == channel) {
        expected_idx_ = (expected_idx_ + 1) % n;
        return;
    }
    if (pattern_synced_) {
        stats_.pattern_order_errors++;
    }
    // (Re)synchronize: a DMA frame need not start at slot 0. Pick the first slot with this channel
    // whose predecessor differs, so repeated MIC slots resolve deterministically.
    for (size_t i = 0; i < n; ++i) {
        if (pattern_raw_channel_[i] == channel) {
            expected_idx_ = (i + 1) % n;
            pattern_synced_ = true;
            return;
        }
    }
}

void AnalogAdcEngine::route(uint32_t channel, uint16_t raw)
{
    stats_.total_results++;
    if (channel == mic_channel_) {
        stats_.microphone_results++;
        mic_.results_seen++;
        if (mic_.samples_stored < cfg_.mic.capacity) {
            cfg_.mic.data[mic_.samples_stored++] = raw;
            mic_.raw_sum += raw;
            mic_.raw_sum_sq += static_cast<uint64_t>(raw) * raw;
            if (raw < mic_.raw_min) mic_.raw_min = raw;
            if (raw > mic_.raw_max) mic_.raw_max = raw;
            if (board::adc_code_near_low(raw)) mic_.low_rail_count++;
            if (board::adc_code_near_high(raw)) mic_.high_rail_count++;
        } else {
            mic_.destination_overflow_count++;  // keep draining; never overwrite
        }
        return;
    }
    Accumulator* acc = nullptr;
    if (channel == board_channel_) {
        stats_.board_temp_results++;
        acc = &board_acc_;
    } else if (channel == cow_channel_) {
        stats_.cow_temp_results++;
        acc = &cow_acc_;
    } else {
        stats_.unexpected_channel_results++;
        return;
    }
    if (!cfg_.temperature_buckets) {
        return;
    }
    acc->sum += raw;
    acc->count++;
    if (raw < acc->min) acc->min = raw;
    if (raw > acc->max) acc->max = raw;
    if (board::adc_code_near_low(raw)) acc->near_low++;
    if (board::adc_code_near_high(raw)) acc->near_high++;
}

void AnalogAdcEngine::emit_bucket(AnalogChannelId ch, Accumulator& acc, uint32_t bucket_index, uint32_t end_ms)
{
    AnalogBucketResult r = {};
    r.channel = ch;
    r.bucket_index = bucket_index;
    r.end_offset_ms = end_ms;
    r.raw_count = acc.count;
    if (acc.count > 0) {
        r.raw_mean = static_cast<uint16_t>((acc.sum + acc.count / 2) / acc.count);
        r.raw_min = acc.min;
        r.raw_max = acc.max;
    }
    r.near_low_count = acc.near_low;
    r.near_high_count = acc.near_high;
    if (xQueueSend(bucket_queue_, &r, 0) != pdTRUE) {
        stats_.bucket_queue_overflow++;
    }
    acc.reset();
}

void AnalogAdcEngine::close_buckets_until(uint32_t offset_ms)
{
    if (!cfg_.temperature_buckets) {
        return;
    }
    while ((next_bucket_index_ + 1) * config::TEMP_BUCKET_MS <= offset_ms) {
        const uint32_t end_ms = (next_bucket_index_ + 1) * config::TEMP_BUCKET_MS;
        if (board_channel_ != 0xFF) emit_bucket(AnalogChannelId::BOARD_TEMP, board_acc_, next_bucket_index_, end_ms);
        if (cow_channel_ != 0xFF) emit_bucket(AnalogChannelId::COW_TEMP, cow_acc_, next_bucket_index_, end_ms);
        next_bucket_index_++;
    }
}

void AnalogAdcEngine::process_frame(const uint8_t* data, uint32_t len)
{
    stats_.frames_received++;
    for (uint32_t i = 0; i + SOC_ADC_DIGI_RESULT_BYTES <= len; i += SOC_ADC_DIGI_RESULT_BYTES) {
        const auto* p = reinterpret_cast<const adc_digi_output_data_t*>(&data[i]);
        const uint32_t unit = p->type2.unit;
        const uint32_t channel = p->type2.channel;
        const uint32_t raw = p->type2.data;
        if (unit != 0 /* ADC1 */ || channel >= SOC_ADC_CHANNEL_NUM(0) ||
            raw > static_cast<uint32_t>(board::adc_max_code())) {
            stats_.invalid_results++;
            continue;
        }
        check_pattern(channel);
        route(channel, static_cast<uint16_t>(raw));
    }
    const int64_t now = esp_timer_get_time();
    const uint32_t offset_ms = static_cast<uint32_t>((now - static_cast<int64_t>(start_us_)) / 1000);
    close_buckets_until(offset_ms);
}

esp_err_t AnalogAdcEngine::stop()
{
    if (!running_) {
        return ESP_OK;
    }
    stop_requested_ = true;
    bool task_exited = xSemaphoreTake(task_done_, pdMS_TO_TICKS(kTaskStopWaitMs)) == pdTRUE;
    task_ = nullptr;
    // Process frames already queued in the driver pool (converted before the stop request).
    // adc_continuous_read() is only valid while started, so this drain precedes the stop.
    // Bounded by frame count: the pool is finite.
    if (task_exited) {
        const int max_frames = static_cast<int>(config::ADC_POOL_BYTES / config::ADC_CONV_FRAME_BYTES) + 1;
        for (int guard = 0; guard < max_frames; ++guard) {
            uint32_t len = 0;
            if (adc_continuous_read(handle_, frame_, config::ADC_CONV_FRAME_BYTES, &len, 0) != ESP_OK || len == 0) {
                break;
            }
            process_frame(frame_, len);
        }
    } else {
        ESP_LOGE(TAG, "acquisition task did not exit within %u ms", static_cast<unsigned>(kTaskStopWaitMs));
    }
    esp_err_t err = adc_continuous_stop(handle_);
    stop_us_ = static_cast<uint64_t>(esp_timer_get_time());
    stats_.pool_overflow_count = pool_ovf_isr_count_;
    stats_.board_partial_bucket_conversions = board_acc_.count;
    stats_.cow_partial_bucket_conversions = cow_acc_.count;
    cleanup_handle();
    running_ = false;
    return err;
}

bool AnalogAdcEngine::pop_bucket(AnalogBucketResult& out)
{
    return bucket_queue_ != nullptr && xQueueReceive(bucket_queue_, &out, 0) == pdTRUE;
}

AnalogAdcEngine& analog_engine()
{
    static AnalogAdcEngine instance;
    return instance;
}

}  // namespace cownect::sensors
