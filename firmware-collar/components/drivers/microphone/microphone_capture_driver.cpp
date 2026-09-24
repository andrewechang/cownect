#include "microphone_capture_driver.h"

#include "board_adc.h"
#include "cownect_config.h"
#include "esp_log.h"

namespace cownect::sensors {
namespace {
constexpr const char* TAG = "MIC";
constexpr AnalogChannelId kMicOnlyPattern[] = {AnalogChannelId::MIC};
}  // namespace

esp_err_t MicrophoneCaptureDriver::init()
{
    if (buf_.data == nullptr || buf_.capacity == 0) {
        ESP_LOGE(TAG, "PSRAM capture buffer not allocated - microphone unavailable");
        return ESP_ERR_NO_MEM;
    }
    board::AdcChannelMap map;
    esp_err_t err = board::adc_channel_for(board::AnalogInput::MIC, map);
    if (err != ESP_OK) {
        return err;
    }
    initialized_ = true;
    ESP_LOGI(TAG, "GPIO%d ADC1_CH%d capacity=%u samples", board::adc_gpio_for(board::AnalogInput::MIC),
             static_cast<int>(map.channel), static_cast<unsigned>(buf_.capacity));
    return ESP_OK;
}

esp_err_t MicrophoneCaptureDriver::start_capture()
{
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }
    stats_ = {};
    if (integrated_) {
        // Engine started by SensorManager with the shared pattern.
        capturing_ = engine_.running();
        return capturing_ ? ESP_OK : ESP_ERR_INVALID_STATE;
    }
    AnalogEngineConfig cfg = {};
    cfg.pattern = kMicOnlyPattern;
    cfg.pattern_len = 1;
    cfg.sample_freq_hz = config::MIC_SAMPLE_RATE_HZ;
    cfg.mic = {buf_.data, buf_.capacity};
    cfg.temperature_buckets = false;
    esp_err_t err = engine_.start(cfg);  // refuses if ADC1 is owned in one-shot mode
    if (err != ESP_OK) {
        return err;
    }
    capturing_ = true;
    ESP_LOGI(TAG, "continuous start rate=%u", static_cast<unsigned>(config::MIC_SAMPLE_RATE_HZ));
    return ESP_OK;
}

esp_err_t MicrophoneCaptureDriver::service()
{
    // Acquisition runs in the ADC engine task; nothing to do in the caller's context.
    return capturing_ ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t MicrophoneCaptureDriver::stop_capture()
{
    if (!capturing_) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = ESP_OK;
    if (!integrated_) {
        err = engine_.stop();
    }
    stats_ = microphone_stats_from_engine(engine_.mic_stats(), engine_.stats(), engine_.start_us(),
                                          integrated_ && engine_.running() ? 0 : engine_.stop_us());
    capturing_ = false;
    ESP_LOGI(TAG, "stop samples=%u mean=%.1f rms_ac=%.2f min=%u max=%u pool_ovf=%u dst_ovf=%u adc_err=%u rate=%.1fHz",
             static_cast<unsigned>(stats_.samples_stored), stats_.raw_mean, stats_.raw_rms_ac,
             static_cast<unsigned>(stats_.raw_min), static_cast<unsigned>(stats_.raw_max),
             static_cast<unsigned>(stats_.driver_pool_overflow_count),
             static_cast<unsigned>(stats_.destination_overflow_count), static_cast<unsigned>(stats_.adc_error_count),
             stats_.effective_sample_rate_hz);
    return err;
}

}  // namespace cownect::sensors
