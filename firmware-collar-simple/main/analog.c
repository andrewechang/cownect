// analog.c - continuous ADC for microphone + temperatures.
#include "analog.h"
#include <math.h>
#include <string.h>
#include "capture.h"
#include "esp_adc/adc_continuous.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"

static const char *TAG = "analog";

#define CH_BOARD_TEMP   ADC_CHANNEL_0   // GPIO1
#define CH_COW_TEMP     ADC_CHANNEL_1   // GPIO2
#define CH_MIC          ADC_CHANNEL_4   // GPIO5
// Readings per temperature channel in one TEMP_REPORT_MS period (each temp is 1 of 4 scan slots)
#define TEMP_READINGS_PER_PERIOD ((ADC_TOTAL_HZ / 4) * TEMP_REPORT_MS / 1000)

static adc_continuous_handle_t s_adc;
static adc_cali_handle_t s_cali_board, s_cali_cow;
static uint8_t s_frame[ADC_FRAME_BYTES];
static volatile uint32_t s_overflows;

// Running sums for the current temperature period
static uint32_t s_board_sum, s_board_n, s_cow_sum, s_cow_n;

// Called by the ADC driver (in interrupt context) when its buffer is full and
// new samples are being lost. Only counts; the count is reported after the capture.
static bool IRAM_ATTR on_overflow(adc_continuous_handle_t h, const adc_continuous_evt_data_t *d, void *u)
{
    s_overflows++;
    return false;
}

// Creates an ESP-IDF calibration handle for one ADC1 channel, used to turn raw
// codes into millivolts. Returns NULL (and logs a warning) if not available.
static adc_cali_handle_t make_cali(adc_channel_t ch)
{
    adc_cali_curve_fitting_config_t cfg = {
        .unit_id = ADC_UNIT_1, .chan = ch, .atten = ADC_ATTEN_SETTING, .bitwidth = ADC_BITWIDTH_12,
    };
    adc_cali_handle_t h = NULL;
    if (adc_cali_create_scheme_curve_fitting(&cfg, &h) != ESP_OK) {
        ESP_LOGW(TAG, "no ADC calibration for channel %d", ch);
        return NULL;
    }
    return h;
}

// 1. creates the calibration for the two temperature channels (first call only)
// 2. creates the continuous-ADC driver with a pool of ADC_POOL_BYTES
// 3. sets the scan pattern MIC, BOARD, MIC, COW (12 dB, 12-bit) at ADC_TOTAL_HZ
// 4. registers the overflow counter and starts sampling
// 5. clears the counters for this capture
bool analog_start(capture_t *c)
{
    if (!s_cali_board) s_cali_board = make_cali(CH_BOARD_TEMP);
    if (!s_cali_cow) s_cali_cow = make_cali(CH_COW_TEMP);

    adc_continuous_handle_cfg_t hcfg = { .max_store_buf_size = ADC_POOL_BYTES, .conv_frame_size = ADC_FRAME_BYTES };
    if (adc_continuous_new_handle(&hcfg, &s_adc) != ESP_OK) return false;

    const adc_channel_t order[4] = {CH_MIC, CH_BOARD_TEMP, CH_MIC, CH_COW_TEMP};
    adc_digi_pattern_config_t pattern[4];
    for (int i = 0; i < 4; i++) {
        pattern[i] = (adc_digi_pattern_config_t){
            .atten = ADC_ATTEN_SETTING, .channel = order[i], .unit = ADC_UNIT_1, .bit_width = ADC_BITWIDTH_12,
        };
    }
    adc_continuous_config_t cfg = {
        .pattern_num = 4,
        .adc_pattern = pattern,
        .sample_freq_hz = ADC_TOTAL_HZ,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE2,   // each result carries its channel number
    };
    adc_continuous_evt_cbs_t cbs = { .on_pool_ovf = on_overflow };
    if (adc_continuous_config(s_adc, &cfg) != ESP_OK ||
        adc_continuous_register_event_callbacks(s_adc, &cbs, NULL) != ESP_OK ||
        adc_continuous_start(s_adc) != ESP_OK) {
        ESP_LOGE(TAG, "continuous ADC start failed");
        adc_continuous_deinit(s_adc);
        s_adc = NULL;
        return false;
    }
    s_overflows = 0;
    s_board_sum = s_board_n = s_cow_sum = s_cow_n = 0;
    ESP_LOGI(TAG, "ADC running at %d Hz (mic %d Hz, temperatures averaged every %d ms)",
             ADC_TOTAL_HZ, MIC_SAMPLE_RATE_HZ, TEMP_REPORT_MS);
    return true;
}

// Converts a raw ADC code to calibrated millivolts; returns -1 if there is no calibration.
static int32_t to_mv(adc_cali_handle_t cali, int32_t raw)
{
    int mv = 0;
    if (cali && adc_cali_raw_to_voltage(cali, raw, &mv) == ESP_OK) return mv;
    return -1;
}

// Closes one board-temperature period: averages the raw readings, converts to mV,
// then to degrees C with the MCP9700 formula  T = (mV - 500) / 10.
// Readings outside 100..1750 mV (the sensor's -40..125 C range) are marked invalid.
static void finish_board(capture_t *c)
{
    if (s_board_n == 0 || c->board_temp_count >= TEMP_MAX_SAMPLES) return;
    temp_sample_t *t = &c->board_temp[c->board_temp_count];
    t->ts_ms = c->board_temp_count * TEMP_REPORT_MS;
    t->adc_raw = (int32_t)(s_board_sum / s_board_n);
    t->mv = to_mv(s_cali_board, t->adc_raw);
    t->resistance_ohm = NAN;
    t->valid = t->mv >= 100 && t->mv <= 1750;
    t->temp_c = t->valid ? (t->mv - MCP9700_V0_MV) / MCP9700_MV_PER_C : NAN;
    c->board_temp_count++;
    s_board_sum = s_board_n = 0;
}

// Closes one cow-temperature period: averages the raw readings, converts to mV,
// works out the thermistor resistance from the voltage divider, then the
// temperature with the Beta formula. Voltages at 0 or at the supply are invalid.
static void finish_cow(capture_t *c)
{
    if (s_cow_n == 0 || c->cow_temp_count >= TEMP_MAX_SAMPLES) return;
    temp_sample_t *t = &c->cow_temp[c->cow_temp_count];
    t->ts_ms = c->cow_temp_count * TEMP_REPORT_MS;
    t->adc_raw = (int32_t)(s_cow_sum / s_cow_n);
    t->mv = to_mv(s_cali_cow, t->adc_raw);
    // Thermistor from the ADC pin to ground, fixed resistor to 3.3 V:
    //   R_ntc = R_fixed * V / (Vsupply - V)
    t->valid = t->mv > 0 && t->mv < NTC_SUPPLY_MV;
    if (t->valid) {
        t->resistance_ohm = NTC_FIXED_R_OHM * t->mv / (NTC_SUPPLY_MV - t->mv);
        // Beta formula: 1/T = 1/T25 + ln(R/R25)/B
        float inv_t = 1.0f / 298.15f + logf(t->resistance_ohm / NTC_R25_OHM) / NTC_BETA;
        t->temp_c = 1.0f / inv_t - 273.15f;
    } else {
        t->resistance_ohm = NAN;
        t->temp_c = NAN;
    }
    c->cow_temp_count++;
    s_cow_sum = s_cow_n = 0;
}

// Reads one frame from the driver and looks at the channel of every result:
//   microphone -> stored in c->mic[] (until the buffer is full)
//   board/cow  -> added to the running sum; after TEMP_READINGS_PER_PERIOD
//                 readings the period is closed with finish_board()/finish_cow()
void analog_poll(capture_t *c)
{
    uint32_t len = 0;
    if (adc_continuous_read(s_adc, s_frame, ADC_FRAME_BYTES, &len, 20) != ESP_OK) return;

    for (uint32_t i = 0; i + SOC_ADC_DIGI_RESULT_BYTES <= len; i += SOC_ADC_DIGI_RESULT_BYTES) {
        const adc_digi_output_data_t *r = (const adc_digi_output_data_t *)&s_frame[i];
        uint32_t ch = r->type2.channel;
        uint32_t value = r->type2.data;
        if (ch == CH_MIC) {
            if (c->mic_count < MIC_MAX_SAMPLES) c->mic[c->mic_count++] = (uint16_t)value;
        } else if (ch == CH_BOARD_TEMP) {
            s_board_sum += value;
            if (++s_board_n >= TEMP_READINGS_PER_PERIOD) finish_board(c);
        } else if (ch == CH_COW_TEMP) {
            s_cow_sum += value;
            if (++s_cow_n >= TEMP_READINGS_PER_PERIOD) finish_cow(c);
        }
    }
}

// Stops and deletes the continuous-ADC driver, keeps an unfinished temperature
// period only if it has at least half the readings, and copies the overflow count.
void analog_stop(capture_t *c)
{
    if (!s_adc) return;
    adc_continuous_stop(s_adc);
    adc_continuous_deinit(s_adc);
    s_adc = NULL;
    if (s_board_n >= TEMP_READINGS_PER_PERIOD / 2) finish_board(c);
    if (s_cow_n >= TEMP_READINGS_PER_PERIOD / 2) finish_cow(c);
    c->adc_overflows = s_overflows;
}
