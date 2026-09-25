// analog.c - continuous ADC: microphone samples + averaged temperatures.
#include "analog.h"
#include <math.h>
#include "esp_adc/adc_continuous.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "analog";

#define CH_BOARD   ADC_CHANNEL_0       // GPIO1
#define CH_COW     ADC_CHANNEL_1       // GPIO2
#define CH_MIC     ADC_CHANNEL_4       // GPIO5
#define FRAME_BYTES 1024               // results taken per analog_read() (4 bytes each)
#define POOL_BYTES  32768              // driver buffer, ~125 ms at 64 kHz
// Readings each temperature gets per TEMP_EVERY_MS (it has 1 of the 4 pattern slots)
#define READINGS_PER_TEMP ((ADC_TOTAL_HZ / 4) * TEMP_EVERY_MS / 1000)

static adc_continuous_handle_t adc;
static adc_cali_handle_t cal_board, cal_cow;
static uint8_t frame[FRAME_BYTES];
static volatile uint32_t overflow_count;
static uint32_t board_sum, board_n, cow_sum, cow_n;    // running sums for the current period

// Called by the ADC driver when its buffer is full (samples are being lost). Only counts.
static bool IRAM_ATTR on_overflow(adc_continuous_handle_t h, const adc_continuous_evt_data_t *e, void *u)
{
    overflow_count++;
    return false;
}

// Creates the ESP-IDF calibration for one channel (raw code -> millivolts). NULL if unavailable.
static adc_cali_handle_t make_calibration(adc_channel_t ch)
{
    adc_cali_curve_fitting_config_t cfg = {
        .unit_id = ADC_UNIT_1, .chan = ch, .atten = ADC_ATTEN_SETTING, .bitwidth = ADC_BITWIDTH_12,
    };
    adc_cali_handle_t h = NULL;
    if (adc_cali_create_scheme_curve_fitting(&cfg, &h) != ESP_OK) ESP_LOGW(TAG, "no calibration for channel %d", ch);
    return h;
}

// Reserves the microphone buffer in PSRAM and creates the two calibrations.
bool analog_init(analog_data_t *d)
{
    d->mic = heap_caps_malloc(MIC_MAX_SAMPLES * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    if (!d->mic) {
        ESP_LOGE(TAG, "not enough PSRAM for the microphone");
        return false;
    }
    cal_board = make_calibration(CH_BOARD);
    cal_cow = make_calibration(CH_COW);
    return true;
}

// Sets up the continuous ADC with the pattern MIC, BOARD, MIC, COW and starts it.
bool analog_start(analog_data_t *d)
{
    adc_continuous_handle_cfg_t handle_cfg = { .max_store_buf_size = POOL_BYTES, .conv_frame_size = FRAME_BYTES };
    if (adc_continuous_new_handle(&handle_cfg, &adc) != ESP_OK) return false;

    const adc_channel_t order[4] = {CH_MIC, CH_BOARD, CH_MIC, CH_COW};
    adc_digi_pattern_config_t pattern[4];
    for (int i = 0; i < 4; i++) {
        pattern[i] = (adc_digi_pattern_config_t){
            .atten = ADC_ATTEN_SETTING, .channel = order[i], .unit = ADC_UNIT_1, .bit_width = ADC_BITWIDTH_12,
        };
    }
    adc_continuous_config_t cfg = {
        .pattern_num = 4, .adc_pattern = pattern, .sample_freq_hz = ADC_TOTAL_HZ,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1, .format = ADC_DIGI_OUTPUT_FORMAT_TYPE2,   // results carry their channel
    };
    adc_continuous_evt_cbs_t callbacks = { .on_pool_ovf = on_overflow };
    if (adc_continuous_config(adc, &cfg) != ESP_OK ||
        adc_continuous_register_event_callbacks(adc, &callbacks, NULL) != ESP_OK ||
        adc_continuous_start(adc) != ESP_OK) {
        ESP_LOGE(TAG, "ADC start failed");
        adc_continuous_deinit(adc);
        adc = NULL;
        return false;
    }
    overflow_count = 0;
    board_sum = board_n = cow_sum = cow_n = 0;
    ESP_LOGI(TAG, "ADC running at %d Hz (mic %d Hz)", ADC_TOTAL_HZ, MIC_SAMPLE_RATE_HZ);
    return true;
}

// Turns one averaging period into a temperature value and adds it to the list.
//   board (MCP9700): T = (mV - 500) / 10, valid for 100..1750 mV (-40..125 C)
//   cow (thermistor divider): R = R_fixed * V / (Vsupply - V), then the Beta formula
static void add_temperature(temp_sample_t *list, uint32_t *count, bool is_cow,
                            adc_cali_handle_t cal, uint32_t sum, uint32_t n)
{
    if (n == 0 || *count >= TEMP_MAX_SAMPLES) return;
    temp_sample_t *t = &list[*count];
    t->time_ms = *count * TEMP_EVERY_MS;
    t->adc_raw = (int32_t)(sum / n);
    int mv = -1;
    if (!cal || adc_cali_raw_to_voltage(cal, t->adc_raw, &mv) != ESP_OK) mv = -1;
    t->mv = mv;
    t->resistance_ohm = NAN;
    t->temp_c = NAN;

    if (!is_cow) {
        t->valid = mv >= 100 && mv <= 1750;
        if (t->valid) t->temp_c = (mv - MCP9700_MV_AT_0C) / MCP9700_MV_PER_C;
    } else {
        t->valid = mv > 0 && mv < NTC_SUPPLY_MV;
        if (t->valid) {
            t->resistance_ohm = NTC_FIXED_R_OHM * mv / (NTC_SUPPLY_MV - mv);
            float inv_t = 1.0f / 298.15f + logf(t->resistance_ohm / NTC_R25_OHM) / NTC_BETA;
            t->temp_c = 1.0f / inv_t - 273.15f;
        }
    }
    (*count)++;
}

// Reads one frame and looks at each result's channel:
//   MIC   -> stored; counted as clipped if at 0 or 4095
//   BOARD / COW -> added to the running sum; a full period becomes one temperature value
void analog_read(analog_data_t *d)
{
    uint32_t len = 0;
    if (adc_continuous_read(adc, frame, FRAME_BYTES, &len, 20) != ESP_OK) return;

    for (uint32_t i = 0; i + SOC_ADC_DIGI_RESULT_BYTES <= len; i += SOC_ADC_DIGI_RESULT_BYTES) {
        const adc_digi_output_data_t *r = (const adc_digi_output_data_t *)&frame[i];
        uint32_t value = r->type2.data;
        switch (r->type2.channel) {
        case CH_MIC:
            if (value <= MIC_CLIP_LOW || value >= MIC_CLIP_HIGH) d->mic_clipped++;
            if (d->mic_count < MIC_MAX_SAMPLES) d->mic[d->mic_count++] = (uint16_t)value;
            break;
        case CH_BOARD:
            board_sum += value;
            if (++board_n >= READINGS_PER_TEMP) {
                add_temperature(d->board, &d->board_count, false, cal_board, board_sum, board_n);
                board_sum = board_n = 0;
            }
            break;
        case CH_COW:
            cow_sum += value;
            if (++cow_n >= READINGS_PER_TEMP) {
                add_temperature(d->cow, &d->cow_count, true, cal_cow, cow_sum, cow_n);
                cow_sum = cow_n = 0;
            }
            break;
        }
    }
}

// Stops the ADC. An unfinished last period is kept only if it is at least half full.
void analog_stop(analog_data_t *d)
{
    if (!adc) return;
    adc_continuous_stop(adc);
    adc_continuous_deinit(adc);
    adc = NULL;
    if (board_n >= READINGS_PER_TEMP / 2) add_temperature(d->board, &d->board_count, false, cal_board, board_sum, board_n);
    if (cow_n >= READINGS_PER_TEMP / 2) add_temperature(d->cow, &d->cow_count, true, cal_cow, cow_sum, cow_n);
    d->overflows = overflow_count;
}
