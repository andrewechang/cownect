// test_mode.c - the bring-up tests listed in test_mode.h.
//
// Every test is one function that runs for a few seconds, prints what it sees
// and returns PASS, FAIL or SKIP. test_run() picks which ones to call:
//   TEST_NUMBER = 1..9   -> that test again and again (pause TEST_REPEAT_MS)
//   TEST_NUMBER = TEST_ALL -> tests 1..9 once each, then a PASS/FAIL list
// The tests use the same drivers as the normal firmware (accel.c, gps.c, ...),
// so a test that passes here means the normal cycle will work for that part.
#include "test_mode.h"
#include <math.h>
#include <string.h>
#include "config.h"
#include "capture.h"
#include "data_format.h"
#include "lora.h"
#include "wifi_upload.h"
#include "driver/gpio.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "test";

typedef enum { RESULT_PASS, RESULT_FAIL, RESULT_SKIP } result_t;
static const char *const RESULT_TEXT[] = {"PASS", "FAIL", "SKIP"};

static bool s_mic_buffer_ok;        // capture_init() got the PSRAM microphone buffer
static uint32_t s_lora_packet_no;   // TEST 8: counts up so every packet is different

// ---------------------------------------------------------------- helpers
// Switches the sensor power rail (PERIPH_EN). After switching on, waits
// RAIL_STABILIZE_MS so the sensors and radio are ready.
static void rail(bool on)
{
    gpio_set_level(PIN_PERIPH_EN, on ? 1 : 0);
    if (on) vTaskDelay(pdMS_TO_TICKS(RAIL_STABILIZE_MS));
}

// Clears all readings in the capture but keeps the PSRAM microphone buffer.
static void clear_capture(capture_t *c)
{
    uint16_t *mic = c->analog.mic;
    memset(c, 0, sizeof(*c));
    c->analog.mic = mic;
}

// Milliseconds since `start_us` (a value from esp_timer_get_time()).
static uint32_t ms_since(int64_t start_us)
{
    return (uint32_t)((esp_timer_get_time() - start_us) / 1000);
}

// Converts one raw accelerometer word to g (14-bit value, left-justified).
static float accel_to_g(int16_t raw)
{
    return (raw >> 2) * (ACCEL_MG_PER_DIGIT / 1000.0f);
}

// Short text for the reason of the last reset (power-on, watchdog, crash, ...).
static const char *reset_reason_text(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON:   return "power-on";
    case ESP_RST_SW:        return "software restart";
    case ESP_RST_PANIC:     return "CRASH (panic)";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:       return "WATCHDOG";
    case ESP_RST_DEEPSLEEP: return "wake from deep sleep";
    case ESP_RST_BROWNOUT:  return "BROWNOUT (supply voltage dropped)";
    case ESP_RST_USB:       return "USB reset";
    default:                return "other";
    }
}

// Runs the continuous ADC (microphone + both temperatures together, exactly as
// in the normal capture) for `ms` milliseconds. Returns false if it cannot start.
static bool run_analog(capture_t *c, uint32_t ms)
{
    clear_capture(c);
    if (!s_mic_buffer_ok) {
        ESP_LOGE(TAG, "no PSRAM microphone buffer (see TEST 1)");
        return false;
    }
    if (!analog_start(&c->analog)) return false;          // analog_start() prints the reason
    int64_t start = esp_timer_get_time();
    while (ms_since(start) < ms) analog_read(&c->analog);  // waits for data itself
    analog_stop(&c->analog);
    return true;
}

// ---------------------------------------------------------------- TEST 1
// ESP32 chip: prints chip model/revision, flash and PSRAM size, free memory,
// reset reason and MAC address.
// PASS = the 1.9 MB microphone buffer could be reserved in PSRAM.
static result_t test_1_esp32(capture_t *c)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    uint32_t flash_bytes = 0;
    esp_flash_get_size(NULL, &flash_bytes);
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    ESP_LOGI(TAG, "chip %s rev v%d.%d, %d cores, CPU %d MHz",
             chip.model == CHIP_ESP32S3 ? "ESP32-S3" : "NOT an ESP32-S3",
             chip.revision / 100, chip.revision % 100, chip.cores, CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    ESP_LOGI(TAG, "flash %u MB (board: 16) | PSRAM %u MB (board: 8)",
             (unsigned)(flash_bytes >> 20), (unsigned)(esp_psram_get_size() >> 20));
    ESP_LOGI(TAG, "free memory: internal %u KB, PSRAM %u KB",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
    ESP_LOGI(TAG, "last reset: %s | Wi-Fi MAC %02X:%02X:%02X:%02X:%02X:%02X",
             reset_reason_text(esp_reset_reason()), mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "microphone buffer (%u KB in PSRAM): %s",
             (unsigned)(MIC_MAX_SAMPLES * sizeof(uint16_t) / 1024), s_mic_buffer_ok ? "reserved" : "NOT reserved");
    return s_mic_buffer_ok ? RESULT_PASS : RESULT_FAIL;
}

// ---------------------------------------------------------------- TEST 2
// Power rail + PROG switch: reads the PROG switch, then switches the sensor rail
// ON for 3 s and OFF for 3 s so you can measure SWITCHED_3V3 with a multimeter.
// The ESP32 cannot measure the rail itself, so PASS only means the steps ran;
// the voltages must be checked by you. The rail is left ON for the next tests.
static result_t test_2_power(capture_t *c)
{
    ESP_LOGI(TAG, "PROG switch (GPIO%d) = %s", PIN_MODE_SW,
             gpio_get_level(PIN_MODE_SW) ? "HIGH (normal)" : "LOW (programming)");
    rail(true);
    ESP_LOGI(TAG, "rail ON  (GPIO%d HIGH) -> measure SWITCHED_3V3: should be ~3.3 V", PIN_PERIPH_EN);
    vTaskDelay(pdMS_TO_TICKS(3000));
    rail(false);
    ESP_LOGI(TAG, "rail OFF (GPIO%d LOW)  -> measure SWITCHED_3V3: should be ~0 V", PIN_PERIPH_EN);
    vTaskDelay(pdMS_TO_TICKS(3000));
    rail(true);
    return RESULT_PASS;
}

// ---------------------------------------------------------------- TEST 3
// Accelerometer: starts the LIS2DW12 (checks its chip ID), reads it for 1 s,
// prints the first 3 samples and the average X/Y/Z and |a| in g.
// Lying still, |a| should be about 1.0 g (gravity); tilt the board and watch X/Y/Z change.
// PASS = at least 90 % of the expected samples and no FIFO overruns.
static result_t test_3_accel(capture_t *c)
{
    clear_capture(c);
    if (!accel_start()) return RESULT_FAIL;      // accel_start() prints the reason (e.g. WHO_AM_I)
    int64_t start = esp_timer_get_time();
    while (ms_since(start) < 1000) {
        vTaskDelay(pdMS_TO_TICKS(ACCEL_READ_EVERY_MS));
        accel_read_fifo(&c->accel);
    }
    accel_stop();

    if (c->accel.count == 0) {
        ESP_LOGE(TAG, "no samples read");
        return RESULT_FAIL;
    }
    float sx = 0, sy = 0, sz = 0, smag = 0;
    for (uint32_t i = 0; i < c->accel.count; i++) {
        float x = accel_to_g(c->accel.samples[i].x), y = accel_to_g(c->accel.samples[i].y), z = accel_to_g(c->accel.samples[i].z);
        if (i < 3) ESP_LOGI(TAG, "sample %u: x %+.3f  y %+.3f  z %+.3f g", (unsigned)i, x, y, z);
        sx += x; sy += y; sz += z;
        smag += sqrtf(x * x + y * y + z * z);
    }
    float n = (float)c->accel.count;
    ESP_LOGI(TAG, "%u samples in 1 s (expected ~%d), FIFO overruns %u",
             (unsigned)c->accel.count, ACCEL_RATE_HZ, (unsigned)c->accel.overruns);
    ESP_LOGI(TAG, "average x %+.3f  y %+.3f  z %+.3f g | |a| %.3f g (lying still: ~1.0 g)",
             sx / n, sy / n, sz / n, smag / n);
    bool enough = c->accel.count >= ACCEL_RATE_HZ * 9 / 10;
    return (enough && c->accel.overruns == 0) ? RESULT_PASS : RESULT_FAIL;
}

// ---------------------------------------------------------------- TEST 4
// Microphone: runs the ADC for 1 s and prints the sample count, the lowest /
// highest / average raw value, the RMS with the DC level removed and the
// number of clipped samples.
// Quiet room: small RMS. Clap or talk near it: RMS should rise clearly.
// PASS = at least 95 % of the expected samples, no ADC overflows, and the signal moves.
static result_t test_4_mic(capture_t *c)
{
    if (!run_analog(c, 1000)) return RESULT_FAIL;

    uint32_t n = c->analog.mic_count, lo = 4095, hi = 0;
    double sum = 0, sum_sq = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t v = c->analog.mic[i];
        if (v < lo) lo = v;
        if (v > hi) hi = v;
        sum += v;
        sum_sq += (double)v * v;
    }
    double mean = n ? sum / n : 0;
    double var = n ? sum_sq / n - mean * mean : 0;
    ESP_LOGI(TAG, "%u samples in 1 s (expected ~%d), ADC overflows %u",
             (unsigned)n, MIC_SAMPLE_RATE_HZ, (unsigned)c->analog.overflows);
    ESP_LOGI(TAG, "raw min %u  max %u  average %.0f (of 0..4095) | RMS %.1f  <- clap: this should rise",
             (unsigned)lo, (unsigned)hi, mean, sqrt(var > 0 ? var : 0));
    ESP_LOGI(TAG, "clipped samples (at %d or %d) %u - should be 0 unless the sound is very loud",
             MIC_CLIP_LOW, MIC_CLIP_HIGH, (unsigned)c->analog.mic_clipped);
    if (n > 0 && hi == lo) ESP_LOGW(TAG, "signal never changes: check the microphone and its amplifier");

    bool enough = n >= (uint32_t)MIC_SAMPLE_RATE_HZ * 95 / 100;
    return (enough && c->analog.overflows == 0 && hi > lo) ? RESULT_PASS : RESULT_FAIL;
}

// ---------------------------------------------------------------- TEST 5
// Temperatures: runs the ADC for 3 s and prints every 1-second value of both
// sensors: raw ADC code, millivolts, (cow probe: resistance) and degrees C.
// Board sensor: about room temperature. Cow probe: rises when you hold it in your hand.
// PASS = both sensors gave at least one valid value.
static result_t test_5_temps(capture_t *c)
{
    if (!run_analog(c, 3000)) return RESULT_FAIL;

    bool board_ok = false, cow_ok = false;
    for (uint32_t i = 0; i < c->analog.board_count; i++) {
        const temp_sample_t *t = &c->analog.board[i];
        board_ok |= t->valid;
        if (t->valid) ESP_LOGI(TAG, "board %u s: raw %4d  %4d mV  -> %.2f C",
                               (unsigned)i, (int)t->adc_raw, (int)t->mv, t->temp_c);
        else ESP_LOGW(TAG, "board %u s: raw %4d  %4d mV  -> INVALID (outside 100..1750 mV)",
                      (unsigned)i, (int)t->adc_raw, (int)t->mv);
    }
    for (uint32_t i = 0; i < c->analog.cow_count; i++) {
        const temp_sample_t *t = &c->analog.cow[i];
        cow_ok |= t->valid;
        if (t->valid) ESP_LOGI(TAG, "cow   %u s: raw %4d  %4d mV  %6.0f ohm -> %.2f C",
                               (unsigned)i, (int)t->adc_raw, (int)t->mv, t->resistance_ohm, t->temp_c);
        else ESP_LOGW(TAG, "cow   %u s: raw %4d  %4d mV  -> INVALID (probe unplugged or shorted?)",
                      (unsigned)i, (int)t->adc_raw, (int)t->mv);
    }
    if (c->analog.board_count == 0 && c->analog.cow_count == 0) ESP_LOGE(TAG, "no temperature values");
    return (board_ok && cow_ok) ? RESULT_PASS : RESULT_FAIL;
}

// ---------------------------------------------------------------- TEST 6
// GPS: listens to the GPS for 5 s and prints every NMEA sentence exactly as it
// arrives (lines starting with "$GN..", "$GP..", "$BD.."), then the parsed result.
// Indoors you will see sentences but no fix; outdoors the first fix after
// power-up can take ~35 s (cold start), so keep the test running.
// PASS = at least one sentence with a correct checksum (= wiring and baud rate are right).
static result_t test_6_gps(capture_t *c)
{
    clear_capture(c);
    if (!gps_start()) {
        ESP_LOGE(TAG, "GPS UART could not be started");
        return RESULT_FAIL;
    }
    gps_set_echo(true);
    int64_t start = esp_timer_get_time();
    while (ms_since(start) < 5000) {
        gps_read(&c->gps, ms_since(start));
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    gps_set_echo(false);
    gps_stop();

    const gps_fix_t *last_valid = NULL;
    uint32_t valid = 0;
    for (uint32_t i = 0; i < c->gps.count; i++) {
        if (c->gps.fixes[i].valid) { valid++; last_valid = &c->gps.fixes[i]; }
    }
    uint32_t sats = c->gps.count ? c->gps.fixes[c->gps.count - 1].satellites : 0;
    ESP_LOGI(TAG, "%u position messages (RMC), %u with a fix | satellites %u | bad sentences %u",
             (unsigned)c->gps.count, (unsigned)valid, (unsigned)sats, (unsigned)c->gps.bad_sentences);
    if (last_valid) {
        ESP_LOGI(TAG, "position %.7f, %.7f | speed %.1f m/s", last_valid->lat_e7 / 1e7,
                 last_valid->lon_e7 / 1e7, last_valid->speed_mps);
    }
    if (!c->gps.uart_ok) {
        ESP_LOGE(TAG, "no good NMEA sentence: check GPS TX->GPIO%d, RX<-GPIO%d, %d baud, and the rail",
                 PIN_GPS_RX, PIN_GPS_TX, GPS_BAUD);
    } else if (!last_valid) {
        ESP_LOGW(TAG, "no fix yet: normal indoors, or during the first ~35 s outdoors");
    }
    return c->gps.uart_ok ? RESULT_PASS : RESULT_FAIL;
}

// ---------------------------------------------------------------- TEST 7
// LoRa SPI wiring: resets the SX1262 and reads its sync-word register, which
// must be 0x1424 after a reset. Nothing is transmitted, so no antenna is needed.
// PASS = the register reads 0x1424.
static result_t test_7_lora_spi(capture_t *c)
{
    if (lora_spi_check()) return RESULT_PASS;    // lora_spi_check() prints the register value
    ESP_LOGE(TAG, "check the rail, SPI wires (CS %d, SCK %d, MOSI %d, MISO %d), RST %d and BUSY %d",
             PIN_LORA_CS, PIN_LORA_SCK, PIN_LORA_MOSI, PIN_LORA_MISO, PIN_LORA_RST, PIN_LORA_BUSY);
    return RESULT_FAIL;
}

// ---------------------------------------------------------------- TEST 8
// LoRa transmit: sends one 49-byte telemetry packet in the normal format, so the
// Heltec gateway can decode it. All sensor values are 0 / "not valid"; the
// capture number counts 1, 2, 3, ... so you can see on the gateway that every
// packet arrives. The bytes are printed in hex to compare with the gateway.
// SKIP = the lora.h values or LORA_ANTENNA_CONNECTED are not set yet.
// PASS = the radio reported "TX done" (it does NOT prove the gateway received it).
static result_t test_8_lora_tx(capture_t *c)
{
    if (!lora_config_ready()) return RESULT_SKIP;  // prints which setting is missing

    clear_capture(c);
    c->capture_id = ++s_lora_packet_no;
    summary_t sum;
    memset(&sum, 0, sizeof(sum));
    uint8_t pkt[TELEMETRY_BYTES];
    size_t len = build_telemetry(c, &sum, pkt);

    ESP_LOGI(TAG, "test packet %u (%u bytes):", (unsigned)s_lora_packet_no, (unsigned)len);
    ESP_LOG_BUFFER_HEX(TAG, pkt, len);
    return lora_send(pkt, len) ? RESULT_PASS : RESULT_FAIL;
}

// ---------------------------------------------------------------- TEST 9
// Wi-Fi: connects to the network, prints the IP address and signal strength,
// then opens and closes a TCP connection to the Jetson (no data is sent).
// SKIP = the wifi_upload.h values are not set yet.
// PASS = Wi-Fi connected and the Jetson accepted the TCP connection.
static result_t test_9_wifi(capture_t *c)
{
    if (!wifi_config_ready()) return RESULT_SKIP;  // prints which setting is missing
    return wifi_test_connection() ? RESULT_PASS : RESULT_FAIL;
}

// ---------------------------------------------------------------- runner
typedef result_t (*test_fn)(capture_t *c);

typedef struct {
    test_fn run;
    const char *name;
} test_entry_t;

// The tests in the recommended order. TEST n = entry n-1.
static const test_entry_t TESTS[] = {
    {test_1_esp32,    "ESP32 chip"},
    {test_2_power,    "Power rail + PROG switch"},
    {test_3_accel,    "Accelerometer"},
    {test_4_mic,      "Microphone"},
    {test_5_temps,    "Temperatures"},
    {test_6_gps,      "GPS"},
    {test_7_lora_spi, "LoRa SPI wiring"},
    {test_8_lora_tx,  "LoRa transmit"},
    {test_9_wifi,     "Wi-Fi + Jetson"},
};
#define TEST_COUNT (int)(sizeof(TESTS) / sizeof(TESTS[0]))

// Runs test number `n` (1..TEST_COUNT) once between two header lines and returns its result.
static result_t run_one(capture_t *c, int n)
{
    if (n < 1 || n > TEST_COUNT) return RESULT_SKIP;
    ESP_LOGI(TAG, "========== TEST %d: %s ==========", n, TESTS[n - 1].name);
    result_t r = TESTS[n - 1].run(c);
    if (r == RESULT_FAIL) ESP_LOGE(TAG, "---------- TEST %d: FAIL ----------", n);
    else ESP_LOGI(TAG, "---------- TEST %d: %s ----------", n, RESULT_TEXT[r]);
    return r;
}

// See test_mode.h.
//  1. reserves the PSRAM microphone buffer and switches the sensor rail on
//  2. TEST_ALL: runs tests 1..9 once, prints a PASS/FAIL list, then just waits
//     one test: runs it forever, with TEST_REPEAT_MS (TEST 8: TEST_LORA_TX_EVERY_MS) between runs
void test_run(capture_t *c)
{
    ESP_LOGW(TAG, "TEST MODE (TEST_NUMBER in test_mode.h) - the normal collar cycle is NOT running");
    s_mic_buffer_ok = capture_init(c);
    rail(true);

    if (TEST_NUMBER == TEST_ALL) {
        result_t results[TEST_COUNT];
        for (int n = 1; n <= TEST_COUNT; n++) results[n - 1] = run_one(c, n);
        ESP_LOGI(TAG, "========== RESULTS ==========");
        for (int n = 1; n <= TEST_COUNT; n++) {
            ESP_LOGI(TAG, "TEST %d  %-26s %s", n, TESTS[n - 1].name, RESULT_TEXT[results[n - 1]]);
        }
        ESP_LOGI(TAG, "done - press the reset button to run all tests again");
        while (true) vTaskDelay(pdMS_TO_TICKS(60000));
    }

    uint32_t pause_ms = (TEST_NUMBER == 8) ? TEST_LORA_TX_EVERY_MS : TEST_REPEAT_MS;
    while (true) {
        run_one(c, TEST_NUMBER);
        vTaskDelay(pdMS_TO_TICKS(pause_ms));
    }
}
