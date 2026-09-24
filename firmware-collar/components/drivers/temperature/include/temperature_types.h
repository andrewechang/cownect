#pragma once

#include <cstddef>
#include <cstdint>

namespace cownect::sensors {

enum class TemperatureStatus : uint8_t {
    OK = 0,
    ADC_ERROR = 1,
    ADC_CALIBRATION_UNAVAILABLE = 2,
    INVALID_VOLTAGE = 3,
    INVALID_RESISTANCE = 4,
    SENSOR_OPEN_SUSPECTED = 5,
    SENSOR_SHORT_SUSPECTED = 6,
    CONVERSION_ERROR = 7,
};

const char* temperature_status_name(TemperatureStatus s);

// When status != OK, temperature_c (and resistance_ohm) are NaN - never a plausible fake value.
struct BoardTemperatureSample {
    uint32_t timestamp_ms;  // relative to shared capture start
    int32_t adc_raw;
    int32_t millivolts;
    bool millivolts_valid;
    float temperature_c;
    TemperatureStatus status;
};

struct CowTemperatureSample {
    uint32_t timestamp_ms;
    int32_t adc_raw;
    int32_t millivolts;
    bool millivolts_valid;
    float resistance_ohm;
    float temperature_c;
    TemperatureStatus status;
};

struct TemperatureCaptureStats {
    uint32_t board_samples_attempted;
    uint32_t board_samples_valid;
    uint32_t board_adc_errors;
    uint32_t board_conversion_errors;

    uint32_t cow_samples_attempted;
    uint32_t cow_samples_valid;
    uint32_t cow_adc_errors;
    uint32_t cow_conversion_errors;
    uint32_t cow_open_suspected;
    uint32_t cow_short_suspected;

    uint32_t buffer_overflows;
};

struct TemperatureCaptureBuffer {
    BoardTemperatureSample* board;
    CowTemperatureSample* cow;
    size_t capacity;
    size_t board_count;
    size_t cow_count;
};

// Statistics bookkeeping shared by standalone tests and SensorManager.
void temperature_account_board(TemperatureCaptureStats& stats, const BoardTemperatureSample& s);
void temperature_account_cow(TemperatureCaptureStats& stats, const CowTemperatureSample& s);

}  // namespace cownect::sensors
