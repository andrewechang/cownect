// gps.c - reads NMEA text from the GPS and keeps the RMC/GGA information we need.
#include "gps.h"
#include <stdlib.h>
#include <string.h>
#include "capture.h"
#include "driver/uart.h"
#include "esp_log.h"

static const char *TAG = "gps";

#define NMEA_LINE_MAX 100      // longest NMEA sentence we accept
#define MAX_FIELDS    20

static char s_line[NMEA_LINE_MAX];
static size_t s_len;
static uint8_t s_last_quality;     // latest values from GGA, attached to the next RMC fix
static uint8_t s_last_sats;
static bool s_installed;

// Installs the UART driver (GPS_UART_PORT, GPS_BAUD, 8N1) on the GPS pins and
// resets the sentence buffer. Returns false if the driver cannot be installed.
bool gps_start(void)
{
    uart_config_t cfg = {
        .baud_rate = GPS_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    if (uart_driver_install(GPS_UART_PORT, GPS_RX_BUFFER_BYTES, 0, 0, NULL, 0) != ESP_OK) return false;
    s_installed = true;
    if (uart_param_config(GPS_UART_PORT, &cfg) != ESP_OK ||
        uart_set_pin(GPS_UART_PORT, PIN_GPS_TX, PIN_GPS_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
        gps_stop();
        return false;
    }
    s_len = 0;
    s_last_quality = 0;
    s_last_sats = 0;
    ESP_LOGI(TAG, "UART%d ready at %d baud", GPS_UART_PORT, GPS_BAUD);
    return true;
}

// Checks the NMEA checksum: the XOR of all characters between '$' and '*'
// must equal the two hex digits after '*'.
static bool checksum_ok(const char *line)
{
    const char *star = strchr(line, '*');
    if (line[0] != '$' || star == NULL || strlen(star) < 3) return false;
    uint8_t sum = 0;
    for (const char *p = line + 1; p < star; p++) sum ^= (uint8_t)*p;
    return sum == (uint8_t)strtol(star + 1, NULL, 16);
}

// Splits "a,b,,d" into fields in place (commas become '\0'), keeping empty
// fields so field numbers stay correct. Returns the number of fields.
static int split_fields(char *line, char **fields)
{
    int n = 0;
    char *star = strchr(line, '*');
    if (star) *star = '\0';
    fields[n++] = line;
    for (char *p = line; *p && n < MAX_FIELDS; p++) {
        if (*p == ',') {
            *p = '\0';
            fields[n++] = p + 1;
        }
    }
    return n;
}

// Converts an NMEA position "ddmm.mmmm" (degrees + minutes) and its hemisphere
// letter (N/S/E/W) into degrees x 10,000,000 (negative for S and W).
static int32_t nmea_to_e7(const char *value, const char *hemi)
{
    double v = atof(value);
    int deg = (int)(v / 100);
    double deg_total = deg + (v - deg * 100) / 60.0;
    if (hemi[0] == 'S' || hemi[0] == 'W') deg_total = -deg_total;
    return (int32_t)(deg_total * 1e7);
}

// Handles one complete sentence:
//   GGA -> remember fix quality and satellite count
//   RMC -> store a new fix (time, valid flag, position, speed, course)
// Sentences with a bad checksum are counted and ignored.
static void handle_sentence(capture_t *c, char *line, uint32_t now_ms)
{
    if (!checksum_ok(line)) {
        c->gps_bad_sentences++;
        return;
    }
    c->gps_uart_ok = true;

    char *f[MAX_FIELDS];
    int n = split_fields(line, f);
    const char *type = f[0] + 3;              // skip "$GP" / "$GN" / "$BD"

    if (strcmp(type, "GGA") == 0 && n >= 8) {
        s_last_quality = (uint8_t)atoi(f[6]);
        s_last_sats = (uint8_t)atoi(f[7]);
    } else if (strcmp(type, "RMC") == 0 && n >= 9) {
        if (c->gps_count >= GPS_MAX_FIXES) return;
        gps_fix_t *fix = &c->gps[c->gps_count++];
        memset(fix, 0, sizeof(*fix));
        fix->offset_ms = now_ms;
        fix->valid = f[2][0] == 'A';
        if (fix->valid) {
            fix->lat_e7 = nmea_to_e7(f[3], f[4]);
            fix->lon_e7 = nmea_to_e7(f[5], f[6]);
            fix->speed_mps = (float)(atof(f[7]) * 0.514444);   // knots -> m/s
            fix->course_deg = (float)atof(f[8]);
        }
        fix->fix_quality = s_last_quality;
        fix->satellites = s_last_sats;
    }
}

// Reads whatever bytes are waiting in the UART (without waiting), collects them
// into a line from '$' to end-of-line, and passes each finished line to
// handle_sentence(). Over-long lines are thrown away.
void gps_poll(capture_t *c, uint32_t now_ms)
{
    uint8_t buf[128];
    int got;
    while ((got = uart_read_bytes(GPS_UART_PORT, buf, sizeof(buf), 0)) > 0) {
        for (int i = 0; i < got; i++) {
            char ch = (char)buf[i];
            if (ch == '$') s_len = 0;                        // start of a new sentence
            if (ch == '\r' || ch == '\n') {
                if (s_len > 0) {
                    s_line[s_len] = '\0';
                    handle_sentence(c, s_line, now_ms);
                    s_len = 0;
                }
            } else if (s_len < NMEA_LINE_MAX - 1) {
                s_line[s_len++] = ch;
            } else {
                s_len = 0;                                   // too long: throw away
                c->gps_bad_sentences++;
            }
        }
    }
}

// Removes the UART driver if it was installed.
void gps_stop(void)
{
    if (s_installed) {
        uart_driver_delete(GPS_UART_PORT);
        s_installed = false;
    }
}
