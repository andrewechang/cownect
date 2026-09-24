#include "sensor_manager.h"

#include <cmath>
#include "board_adc.h"
#include "board_identity.h"
#include "board_power.h"
#include "capture_session.h"
#include "cownect_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rtc_state.h"

namespace cownect::sensors {
namespace {
constexpr const char* TAG = "CAP";
constexpr uint32_t kServicePeriodMs = 10;
}  // namespace

SensorSet::SensorSet()
    : accel_buffer{nullptr, 0, 0},
      gps_buffer{nullptr, 0, 0},
      mic_buffer{nullptr, 0},
      accelerometer(accel_buffer),
      gps(gps_buffer),
      microphone(analog_engine(), mic_buffer),
      board_temperature(preset_adc),
      cow_temperature(preset_adc, mf58_default_model(), mf58_default_electrical())
{
    rebind();
}

void SensorSet::rebind()
{
    data::CaptureStorage& st = data::capture_storage();
    accel_buffer.data = st.accel;
    accel_buffer.capacity = st.accel_capacity;
    gps_buffer.data = st.gps;
    gps_buffer.capacity = st.gps_capacity;
    mic_buffer.data = st.mic;
    mic_buffer.capacity = st.mic_capacity;
}

SensorSet& sensor_set()
{
    static SensorSet instance;
    instance.rebind();
    return instance;
}

SensorManager& sensor_manager()
{
    static SensorManager instance;
    return instance;
}

esp_err_t SensorManager::init()
{
    sensor_set();
    return ESP_OK;
}

bool SensorManager::capture_target_reached() const
{
    return capturing_ && esp_timer_get_time() >= target_us_;
}

esp_err_t SensorManager::begin_capture(data::CaptureSession& s, uint32_t duration_ms)
{
    if (capturing_) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = data::capture_session_reset(s);
    if (err != ESP_OK) {
        return err;  // previous capture still owned by a transfer
    }
    SensorSet& set = sensor_set();
    set.accel_buffer.count = 0;
    set.gps_buffer.count = 0;
    session_ = &s;

    // Power: only through the board API; stabilization is the centralized DEVELOPMENT DEFAULT.
    if (!board_peripherals_commanded_enabled()) {
        err = board_peripherals_set_enabled(true);
        if (err != ESP_OK) {
            return err;
        }
    }
    board_peripherals_wait_stabilization();

    // Initialize drivers independently: one failure must not stop the others.
    s.status.accel_error = set.accelerometer.init();
    s.status.gps_error = set.gps.init();
    const bool mic_available = set.microphone.init() == ESP_OK;

    s.id.device_id = board_device_id();
    s.id.capture_id = data::capture_id_allocate();
    s.diagnostics.requested_capture_us = static_cast<uint64_t>(duration_ms) * 1000;
    start_us_ = esp_timer_get_time();
    target_us_ = start_us_ + static_cast<int64_t>(duration_ms) * 1000;
    s.capture_start_us = static_cast<uint64_t>(start_us_);
    if (s.cycle_start_us == 0) {
        s.cycle_start_us = s.capture_start_us;
    }

    accel_running_ = false;
    if (s.status.accel_error == ESP_OK) {
        s.status.accel_error = set.accelerometer.start_capture();
        accel_running_ = s.status.accel_error == ESP_OK;
    }
    gps_running_ = false;
    if (s.status.gps_error == ESP_OK) {
        s.status.gps_error = set.gps.start_capture();
        gps_running_ = s.status.gps_error == ESP_OK;
    }

    engine_running_ = false;
    oneshot_temps_ = false;
    next_oneshot_index_ = 0;
    engine_stop_err_ = ESP_OK;
    set.board_temperature.set_adc(set.preset_adc);
    set.cow_temperature.set_adc(set.preset_adc);
    if (mic_available) {
        AnalogEngineConfig cfg = {};
        cfg.pattern = kIntegratedPattern;
        cfg.pattern_len = 4;
        cfg.sample_freq_hz = config::INTEGRATED_ADC_TOTAL_HZ;
        cfg.mic = {set.mic_buffer.data, set.mic_buffer.capacity};
        cfg.temperature_buckets = true;
        s.status.analog_error = analog_engine().start(cfg);
        engine_running_ = s.status.analog_error == ESP_OK;
        if (engine_running_) {
            set.microphone.set_integrated_mode(true);
            set.microphone.start_capture();
        }
    } else {
        s.status.analog_error = ESP_ERR_NO_MEM;
    }
    if (!engine_running_) {
        // Shared engine unavailable (no PSRAM buffer or ADC config rejected): the microphone is
        // unavailable for this capture, but temperatures are still sampled at 1 Hz via one-shot.
        if (set.oneshot_adc.init() == ESP_OK) {
            set.board_temperature.set_adc(set.oneshot_adc);
            set.cow_temperature.set_adc(set.oneshot_adc);
            oneshot_temps_ = true;
        }
    }

    last_accel_poll_us_ = start_us_;
    capturing_ = true;
    s.state = data::CaptureState::CAPTURING;
    ESP_LOGI(TAG, "start id=%u duration=%ums accel=%s gps=%s analog=%s", static_cast<unsigned>(s.id.capture_id),
             static_cast<unsigned>(duration_ms), cownect_err_name(s.status.accel_error),
             cownect_err_name(s.status.gps_error), cownect_err_name(s.status.analog_error));
    return ESP_OK;
}

void SensorManager::store_board(const BoardTemperatureSample& b)
{
    data::CaptureSession& s = *session_;
    temperature_account_board(s.diagnostics.temperature, b);
    if (s.board_temperature_count < data::capture_storage().temp_capacity) {
        s.board_temperature_samples[s.board_temperature_count++] = b;
    } else {
        s.diagnostics.temperature.buffer_overflows++;
    }
}

void SensorManager::store_cow(const CowTemperatureSample& c)
{
    data::CaptureSession& s = *session_;
    temperature_account_cow(s.diagnostics.temperature, c);
    if (s.cow_temperature_count < data::capture_storage().temp_capacity) {
        s.cow_temperature_samples[s.cow_temperature_count++] = c;
    } else {
        s.diagnostics.temperature.buffer_overflows++;
    }
}

void SensorManager::handle_bucket(const AnalogBucketResult& b)
{
    SensorSet& set = sensor_set();
    AdcReading r = {};
    esp_err_t err = ESP_ERR_INVALID_SIZE;  // bucket without conversions -> ADC_ERROR sample
    if (b.raw_count > 0) {
        r.raw = b.raw_mean;  // representative raw code, calibrated once (Material 9 section 11)
        r.millivolts_valid = board::adc_raw_to_millivolts(r.raw, r.millivolts);
        err = ESP_OK;
    }
    const int64_t engine_offset_ms = (static_cast<int64_t>(analog_engine().start_us()) - start_us_) / 1000;
    const uint32_t ts = static_cast<uint32_t>(static_cast<int64_t>(b.end_offset_ms) + engine_offset_ms);
    if (b.channel == AnalogChannelId::BOARD_TEMP) {
        set.preset_adc.set_board(r, err);
        BoardTemperatureSample sample = {};
        sample.timestamp_ms = ts;
        set.board_temperature.sample(sample);
        store_board(sample);
    } else if (b.channel == AnalogChannelId::COW_TEMP) {
        set.preset_adc.set_cow(r, err);
        CowTemperatureSample sample = {};
        sample.timestamp_ms = ts;
        set.cow_temperature.sample(sample);
        store_cow(sample);
    }
}

void SensorManager::oneshot_temperature_tick()
{
    const int64_t now = esp_timer_get_time();
    const int64_t due = start_us_ + static_cast<int64_t>(next_oneshot_index_) * config::TEMP_BUCKET_MS * 1000;
    if (now < due) {
        return;
    }
    SensorSet& set = sensor_set();
    const uint32_t ts = static_cast<uint32_t>((now - start_us_) / 1000);
    BoardTemperatureSample b = {};
    b.timestamp_ms = ts;
    set.board_temperature.sample(b);
    store_board(b);
    CowTemperatureSample c = {};
    c.timestamp_ms = ts;
    set.cow_temperature.sample(c);
    store_cow(c);
    next_oneshot_index_++;
}

esp_err_t SensorManager::service()
{
    if (!capturing_) {
        return ESP_ERR_INVALID_STATE;
    }
    SensorSet& set = sensor_set();
    const int64_t t0 = esp_timer_get_time();
    if (accel_running_ && t0 - last_accel_poll_us_ >= static_cast<int64_t>(config::ACCEL_FIFO_POLL_MS) * 1000) {
        last_accel_poll_us_ = t0;
        set.accelerometer.service();  // errors are counted in the driver's stats
    }
    if (gps_running_) {
        set.gps.service();
    }
    AnalogBucketResult b;
    while (engine_running_ && analog_engine().pop_bucket(b)) {
        handle_bucket(b);
    }
    if (oneshot_temps_) {
        oneshot_temperature_tick();
    }
    if (esp_timer_get_time() - t0 > static_cast<int64_t>(config::ACCEL_FIFO_POLL_MS) * 1000) {
        session_->diagnostics.sensor_manager_service_overrun_count++;
    }
    return ESP_OK;
}

void SensorManager::finalize_status()
{
    data::CaptureSession& s = *session_;
    SensorSet& set = sensor_set();
    data::CaptureStatus& st = s.status;
    data::CaptureDiagnostics& d = s.diagnostics;

    d.accelerometer = set.accelerometer.stats();
    s.accel_count = set.accel_buffer.count;
    st.accel_ok = st.accel_error == ESP_OK && s.accel_count > 0;
    st.accel_degraded = d.accelerometer.fifo_overrun_count > 0 || d.accelerometer.dropped_samples > 0 ||
                        d.accelerometer.i2c_error_count > 0;

    d.gps = set.gps.stats();
    s.gps_fix_count = set.gps_buffer.count;
    st.gps_ok = st.gps_error == ESP_OK && d.gps.uart_bytes_received > 0;
    st.gps_had_valid_fix = s.gps_fix_count > 0;
    st.gps_uart_overflow_count = d.gps.uart_overflow_count;
    st.gps_degraded = d.gps.uart_overflow_count > 0;

    if (engine_running_) {
        d.adc = analog_engine().stats();
        d.microphone = set.microphone.stats();
        s.microphone_sample_count = d.microphone.samples_stored;
        st.adc_shared_ok = engine_stop_err_ == ESP_OK && d.adc.driver_error_count == 0;
        st.microphone_ok = st.adc_shared_ok && s.microphone_sample_count > 0;
        st.mic_overflow_count = d.microphone.driver_pool_overflow_count + d.microphone.destination_overflow_count;
        st.microphone_degraded = st.mic_overflow_count > 0 || d.microphone.adc_error_count > 0 ||
                                 d.adc.pattern_order_errors > 0 || d.adc.unexpected_channel_results > 0;
    }
    st.board_temp_ok = d.temperature.board_samples_valid > 0;
    st.cow_temp_ok = d.temperature.cow_samples_valid > 0;
    st.cow_probe_fault = d.temperature.cow_open_suspected > 0 || d.temperature.cow_short_suspected > 0;
}

esp_err_t SensorManager::end_capture()
{
    if (!capturing_) {
        return ESP_ERR_INVALID_STATE;
    }
    SensorSet& set = sensor_set();
    data::CaptureSession& s = *session_;
    // Material 9 section 23 stop order.
    if (engine_running_) {
        engine_stop_err_ = analog_engine().stop();
        AnalogBucketResult b;
        while (analog_engine().pop_bucket(b)) {
            handle_bucket(b);
        }
        set.microphone.stop_capture();
    }
    if (oneshot_temps_) {
        set.oneshot_adc.deinit();
        oneshot_temps_ = false;
    }
    if (accel_running_) {
        set.accelerometer.stop_capture();
    }
    if (gps_running_) {
        set.gps.stop_capture();
    }
    s.capture_end_us = static_cast<uint64_t>(esp_timer_get_time());
    s.diagnostics.actual_capture_us = s.capture_end_us - s.capture_start_us;
    finalize_status();
    engine_running_ = false;
    accel_running_ = false;
    gps_running_ = false;
    capturing_ = false;
    s.state = data::CaptureState::COMPLETE;

    const auto& d = s.diagnostics;
    ESP_LOGI(TAG, "stop id=%u elapsed=%ums", static_cast<unsigned>(s.id.capture_id),
             static_cast<unsigned>(d.actual_capture_us / 1000));
    ESP_LOGI(TAG, "mic=%u adc_pool_ovf=%u dst_ovf=%u pattern_err=%u", static_cast<unsigned>(s.microphone_sample_count),
             static_cast<unsigned>(d.adc.pool_overflow_count), static_cast<unsigned>(d.microphone.destination_overflow_count),
             static_cast<unsigned>(d.adc.pattern_order_errors));
    ESP_LOGI(TAG, "accel=%u fifo_ovr=%u i2c_err=%u dropped=%u", static_cast<unsigned>(s.accel_count),
             static_cast<unsigned>(d.accelerometer.fifo_overrun_count),
             static_cast<unsigned>(d.accelerometer.i2c_error_count), static_cast<unsigned>(d.accelerometer.dropped_samples));
    ESP_LOGI(TAG, "gps_sentences=%u valid_fixes=%u uart_ovf=%u", static_cast<unsigned>(d.gps.framed_sentence_count),
             static_cast<unsigned>(s.gps_fix_count), static_cast<unsigned>(d.gps.uart_overflow_count));
    ESP_LOGI(TAG, "tboard=%u tcow=%u board_valid=%u cow_valid=%u cow_open=%u cow_short=%u",
             static_cast<unsigned>(s.board_temperature_count), static_cast<unsigned>(s.cow_temperature_count),
             static_cast<unsigned>(d.temperature.board_samples_valid), static_cast<unsigned>(d.temperature.cow_samples_valid),
             static_cast<unsigned>(d.temperature.cow_open_suspected), static_cast<unsigned>(d.temperature.cow_short_suspected));
    return ESP_OK;
}

esp_err_t SensorManager::run_capture(data::CaptureSession& s, uint32_t duration_ms)
{
    esp_err_t err = begin_capture(s, duration_ms);
    if (err != ESP_OK) {
        return err;
    }
    while (!capture_target_reached()) {
        service();
        vTaskDelay(pdMS_TO_TICKS(kServicePeriodMs));
    }
    service();
    return end_capture();
}

void SensorManager::ensure_stopped()
{
    if (capturing_) {
        end_capture();
    }
    if (analog_engine().running()) {
        analog_engine().stop();
    }
}

void SensorManager::notify_peripheral_power_lost()
{
    sensor_set().accelerometer.mark_power_lost();
}

}  // namespace cownect::sensors
