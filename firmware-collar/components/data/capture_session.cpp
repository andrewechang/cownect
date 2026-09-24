#include "capture_session.h"

#include "cownect_config.h"
#include "cownect_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

namespace cownect::data {
namespace {
constexpr const char* TAG = "CAPTURE";

// Small arrays: internal RAM (Material 5 section 10, Material 6 section 19).
AccelSample s_accel[config::ACCEL_CAPTURE_MAX_SAMPLES];
GpsFix s_gps[config::GPS_CAPTURE_MAX_FIXES];
BoardTemperatureSample s_board[config::TEMP_CAPTURE_MAX_SAMPLES];
CowTemperatureSample s_cow[config::TEMP_CAPTURE_MAX_SAMPLES];

CaptureStorage s_storage = {s_accel, config::ACCEL_CAPTURE_MAX_SAMPLES, nullptr, 0,
                            s_gps,   config::GPS_CAPTURE_MAX_FIXES,     s_board, s_cow,
                            config::TEMP_CAPTURE_MAX_SAMPLES};
CaptureSession s_session = {};
bool s_init_done = false;
}  // namespace

esp_err_t capture_storage_init()
{
    if (s_init_done) {
        return s_storage.mic != nullptr ? ESP_OK : ESP_ERR_NO_MEM;
    }
    s_init_done = true;
    const size_t bytes = config::MIC_CAPTURE_MAX_SAMPLES * sizeof(uint16_t);
    s_storage.mic = static_cast<uint16_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (s_storage.mic == nullptr) {
        ESP_LOGE(TAG, "PSRAM microphone buffer allocation (%u bytes) FAILED - microphone capture unavailable",
                 static_cast<unsigned>(bytes));
        s_storage.mic_capacity = 0;
        s_session.state = CaptureState::EMPTY;
        return ESP_ERR_NO_MEM;
    }
    s_storage.mic_capacity = config::MIC_CAPTURE_MAX_SAMPLES;
    ESP_LOGI(TAG, "PSRAM microphone buffer %u bytes (%u samples) allocated once", static_cast<unsigned>(bytes),
             static_cast<unsigned>(s_storage.mic_capacity));
    return ESP_OK;
}

CaptureStorage& capture_storage()
{
    return s_storage;
}

bool capture_storage_microphone_available()
{
    return s_storage.mic != nullptr;
}

CaptureSession& capture_session()
{
    return s_session;
}

esp_err_t capture_session_reset(CaptureSession& s)
{
    if (s.state == CaptureState::FROZEN_FOR_TRANSFER) {
        return COWNECT_ERR_CAPTURE_FROZEN;
    }
    s = {};
    s.accel_samples = s_storage.accel;
    s.microphone_raw_samples = s_storage.mic;
    s.gps_fixes = s_storage.gps;
    s.board_temperature_samples = s_storage.board_temp;
    s.cow_temperature_samples = s_storage.cow_temp;
    s.microphone_sample_rate_hz = config::MIC_SAMPLE_RATE_HZ;
    s.status.accel_error = ESP_ERR_INVALID_STATE;
    s.status.gps_error = ESP_ERR_INVALID_STATE;
    s.status.analog_error = ESP_ERR_INVALID_STATE;
    s.state = CaptureState::EMPTY;
    return ESP_OK;
}

void capture_session_freeze(CaptureSession& s)
{
    s.state = CaptureState::FROZEN_FOR_TRANSFER;
}

void capture_session_release(CaptureSession& s)
{
    if (s.state == CaptureState::FROZEN_FOR_TRANSFER) {
        s.state = CaptureState::COMPLETE;
    }
}

uint32_t capture_status_bits(const CaptureStatus& st)
{
    uint32_t b = 0;
    if (st.accel_ok) b |= CAPTURE_BIT_ACCEL_OK;
    if (st.gps_ok) b |= CAPTURE_BIT_GPS_UART_OK;
    if (st.gps_had_valid_fix) b |= CAPTURE_BIT_GPS_HAD_VALID_FIX;
    if (st.microphone_ok) b |= CAPTURE_BIT_MIC_OK;
    if (st.board_temp_ok) b |= CAPTURE_BIT_BOARD_TEMP_OK;
    if (st.cow_temp_ok) b |= CAPTURE_BIT_COW_TEMP_OK;
    if (st.adc_shared_ok) b |= CAPTURE_BIT_ADC_SHARED_OK;
    if (st.accel_degraded) b |= CAPTURE_BIT_ACCEL_DEGRADED;
    if (st.microphone_degraded) b |= CAPTURE_BIT_MIC_DEGRADED;
    if (st.gps_degraded) b |= CAPTURE_BIT_GPS_DEGRADED;
    if (st.cow_probe_fault) b |= CAPTURE_BIT_COW_PROBE_FAULT;
    return b;
}

}  // namespace cownect::data
