// gps.c - collects NMEA sentences from the GPS and keeps RMC (position) + GGA (quality).
#include "gps.h"
#include <stdlib.h>
#include <string.h>
#include "driver/uart.h"
#include "esp_log.h"

static const char *TAG = "gps";

static char line[100];            // the sentence being received
static int line_len;
static uint8_t last_quality;      // from the latest GGA, attached to the next RMC fix
static uint8_t last_satellites;
static bool uart_open;

// Opens the UART at GPS_BAUD on the GPS pins.
bool gps_start(void)
{
    uart_config_t cfg = {
        .baud_rate = GPS_BAUD, .data_bits = UART_DATA_8_BITS, .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1, .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, .source_clk = UART_SCLK_DEFAULT,
    };
    if (uart_driver_install(GPS_UART_PORT, 4096, 0, 0, NULL, 0) != ESP_OK) return false;
    uart_open = true;
    if (uart_param_config(GPS_UART_PORT, &cfg) != ESP_OK ||
        uart_set_pin(GPS_UART_PORT, PIN_GPS_TX, PIN_GPS_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
        gps_stop();
        return false;
    }
    line_len = 0;
    last_quality = last_satellites = 0;
    ESP_LOGI(TAG, "UART ready at %d baud", GPS_BAUD);
    return true;
}

// True if the "*HH" checksum matches the XOR of the characters between '$' and '*'.
static bool checksum_ok(const char *s)
{
    const char *star = strchr(s, '*');
    if (s[0] != '$' || !star || strlen(star) < 3) return false;
    uint8_t sum = 0;
    for (const char *p = s + 1; p < star; p++) sum ^= (uint8_t)*p;
    return sum == (uint8_t)strtol(star + 1, NULL, 16);
}

// "ddmm.mmmm" + N/S/E/W  ->  degrees x 10,000,000
static int32_t to_degrees_e7(const char *value, const char *hemisphere)
{
    double v = atof(value);
    int deg = (int)(v / 100);
    double degrees = deg + (v - deg * 100) / 60.0;
    if (hemisphere[0] == 'S' || hemisphere[0] == 'W') degrees = -degrees;
    return (int32_t)(degrees * 1e7);
}

// Splits the finished sentence at the commas (keeping empty fields) and uses it:
// GGA -> remember quality + satellites, RMC -> store a new fix.
static void use_sentence(gps_data_t *g, uint32_t time_ms)
{
    if (!checksum_ok(line)) {
        g->bad_sentences++;
        return;
    }
    g->uart_ok = true;

    char *f[20];
    int n = 0;
    *strchr(line, '*') = '\0';
    f[n++] = line;
    for (char *p = line; *p && n < 20; p++) {
        if (*p == ',') { *p = '\0'; f[n++] = p + 1; }
    }
    const char *type = f[0] + 3;          // "$GNRMC" -> "RMC"

    if (strcmp(type, "GGA") == 0 && n >= 8) {
        last_quality = (uint8_t)atoi(f[6]);
        last_satellites = (uint8_t)atoi(f[7]);
    } else if (strcmp(type, "RMC") == 0 && n >= 9 && g->count < GPS_MAX_FIXES) {
        gps_fix_t *fix = &g->fixes[g->count++];
        memset(fix, 0, sizeof(*fix));
        fix->time_ms = time_ms;
        fix->valid = f[2][0] == 'A';
        if (fix->valid) {
            fix->lat_e7 = to_degrees_e7(f[3], f[4]);
            fix->lon_e7 = to_degrees_e7(f[5], f[6]);
            fix->speed_mps = (float)(atof(f[7]) * 0.514444);   // knots -> m/s
            fix->course_deg = (float)atof(f[8]);
        }
        fix->fix_quality = last_quality;
        fix->satellites = last_satellites;
    }
}

// Reads waiting bytes; each line from '$' to end-of-line is one sentence.
void gps_read(gps_data_t *g, uint32_t time_ms)
{
    uint8_t buf[128];
    int got;
    while ((got = uart_read_bytes(GPS_UART_PORT, buf, sizeof(buf), 0)) > 0) {
        for (int i = 0; i < got; i++) {
            char c = (char)buf[i];
            if (c == '$') line_len = 0;
            if (c == '\r' || c == '\n') {
                if (line_len > 0) {
                    line[line_len] = '\0';
                    use_sentence(g, time_ms);
                    line_len = 0;
                }
            } else if (line_len < (int)sizeof(line) - 1) {
                line[line_len++] = c;
            } else {
                line_len = 0;                      // line too long: broken, drop it
                g->bad_sentences++;
            }
        }
    }
}

// Closes the UART if it is open.
void gps_stop(void)
{
    if (uart_open) {
        uart_driver_delete(GPS_UART_PORT);
        uart_open = false;
    }
}
