#pragma once

#include "analog_adc_engine.h"
#include "atgm336h_driver.h"
#include "board_temperature.h"
#include "capture_types.h"
#include "cow_temperature.h"
#include "cownect_config.h"
#include "esp_err.h"
#include "lis2dw12_driver.h"
#include "microphone_capture_driver.h"
#include "slow_adc.h"

namespace cownect::sensors {

// Material 9 baseline ADC1 scan pattern at 64 kconversions/s.
inline constexpr AnalogChannelId kIntegratedPattern[4] = {
    AnalogChannelId::MIC, AnalogChannelId::BOARD_TEMP, AnalogChannelId::MIC, AnalogChannelId::COW_TEMP};

// IAdcSlowSampler fed with 1-second aggregates from the continuous engine, so the Material 6
// drivers are reused unchanged in integrated mode.
class PresetSlowSampler final : public IAdcSlowSampler {
public:
    esp_err_t init() override { return ESP_OK; }
    esp_err_t read_board_temperature_channel(AdcReading& out) override { return take(board_, board_err_, out); }
    esp_err_t read_cow_temperature_channel(AdcReading& out) override { return take(cow_, cow_err_, out); }
    void set_board(const AdcReading& r, esp_err_t err) { board_ = r; board_err_ = err; }
    void set_cow(const AdcReading& r, esp_err_t err) { cow_ = r; cow_err_ = err; }

private:
    static esp_err_t take(const AdcReading& src, esp_err_t err, AdcReading& out)
    {
        out = src;
        return err;
    }
    AdcReading board_ = {};
    AdcReading cow_ = {};
    esp_err_t board_err_ = ESP_ERR_INVALID_STATE;
    esp_err_t cow_err_ = ESP_ERR_INVALID_STATE;
};

// The real, single instances of every sensor driver. Development tests use these too.
struct SensorSet {
    SensorSet();
    void rebind();  // refreshes buffer pointers from data::capture_storage()

    AccelCaptureBuffer accel_buffer;
    GpsFixBuffer gps_buffer;
    MicCaptureBuffer mic_buffer;
    Lis2dw12Driver accelerometer;
    Atgm336hDriver gps;
    MicrophoneCaptureDriver microphone;
    OneshotSlowAdc oneshot_adc;
    PresetSlowSampler preset_adc;
    Mcp9700Driver board_temperature;
    Mf58Driver cow_temperature;
};
SensorSet& sensor_set();  // binds buffers to data::capture_storage() on first use

// Owner of one capture transaction (Material 9 section 15). Coordinates drivers; contains no
// register protocols, no communication, no sleep.
class SensorManager {
public:
    esp_err_t init();
    esp_err_t begin_capture(data::CaptureSession& session, uint32_t duration_ms = config::CAPTURE_TIME_MS);
    esp_err_t service();
    esp_err_t end_capture();
    bool is_capturing() const { return capturing_; }
    bool capture_target_reached() const;

    // begin + bounded service loop until the shared target time + end.
    esp_err_t run_capture(data::CaptureSession& session, uint32_t duration_ms);

    // Cleanup guard: stops anything still running (safe after partial start).
    void ensure_stopped();
    // Call after SWITCHED_3V3 was commanded off so drivers re-initialize next time.
    void notify_peripheral_power_lost();

private:
    void handle_bucket(const AnalogBucketResult& b);
    void store_board(const BoardTemperatureSample& s);
    void store_cow(const CowTemperatureSample& s);
    void oneshot_temperature_tick();
    void finalize_status();

    data::CaptureSession* session_ = nullptr;
    bool capturing_ = false;
    bool accel_running_ = false;
    bool gps_running_ = false;
    bool engine_running_ = false;
    bool oneshot_temps_ = false;
    esp_err_t engine_stop_err_ = ESP_OK;
    int64_t start_us_ = 0;
    int64_t target_us_ = 0;
    int64_t last_accel_poll_us_ = 0;
    uint32_t next_oneshot_index_ = 0;
};

SensorManager& sensor_manager();

}  // namespace cownect::sensors
