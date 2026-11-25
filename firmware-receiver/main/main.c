#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "config.h"
#include "lora.h"
#include "oled.h"

static const char *TAG = "receiver";

// HYBRID mode: 49-byte summary (simple_version2 data_format.c build_telemetry)
#define TELEMETRY_BYTES    49
#define TELEMETRY_MAGIC    0x4D43
#define TELEMETRY_VERSION  1
#define TELEMETRY_TYPE     0x10
#define STATUS_TYPE        0x12    // collar status text line

// LORA_FULL mode: feature packet (simple_version2 data_format.c build_features)
//    0 "CM" | 2 version 2 | 3 type 0x11 | 4 device u64 | 12 capture u32
//   16 valid u8 (bit0 GPS, 1 board temp, 2 cow temp, 3 accel, 4 mic) | 17 flags u32
//   21 lat i32 | 25 lon i32 (degrees x 1e7) | 29 GPS satellites u8 | 30 GPS fix quality u8
//   31 board temp i16 | 33 cow temp i16 (C x 100) | 35 capture ms u16 | 37 block ms u16
//   39 block count u8 (B) | 40 second count u8 (S) | 41 loud sound events u8
//   42 B blocks x 20 bytes: mean X/Y/Z i16 mg, ODBA u16 mg, peak u16 mg, moves u8,
//                           sound low/mid/high u8 (dB x 3),
//                           jerk X/Y/Z u16 (derivative: average |da/dt|, g/s x 100)
//      then S bytes: loudness per second (dB x 3)
#define FEATURES_VERSION      2
#define FEATURES_TYPE         0x11
#define FEATURES_HEADER_BYTES 42
#define FEATURES_BLOCK_BYTES  20

#define VALID_GPS    0x01
#define VALID_BOARD  0x02
#define VALID_COW    0x04
#define VALID_ACCEL  0x08
#define VALID_MIC    0x10

static uint16_t read_u16(const uint8_t *data)
{
    return (uint16_t)data[0] |
           ((uint16_t)data[1] << 8);
}

static uint32_t read_u32(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static uint64_t read_u64(const uint8_t *data)
{
    uint64_t value = 0;

    for (int i = 0; i < 8; i++) {
        value |= (uint64_t)data[i] << (8 * i);
    }

    return value;
}

static float read_float(const uint8_t *data)
{
    uint32_t bits = read_u32(data);
    float value;

    memcpy(&value, &bits, sizeof(value));

    return value;
}

static bool valid_telemetry_packet(const uint8_t *packet,
                                   int length)
{
    if (length != TELEMETRY_BYTES) {
        return false;
    }

    if (read_u16(&packet[0]) != TELEMETRY_MAGIC) {
        return false;
    }

    if (packet[2] != TELEMETRY_VERSION) {
        return false;
    }

    if (packet[3] != TELEMETRY_TYPE) {
        return false;
    }

    return true;
}

static void print_telemetry(const uint8_t *packet,
                            int length)
{
    if (!valid_telemetry_packet(packet, length)) {
        ESP_LOGW(TAG,
                 "Ignoring invalid packet with length %d",
                 length);

        printf("{\"type\":\"lora_invalid\",\"length\":%d}\n",
               length);
        return;
    }

    uint64_t device_id = read_u64(&packet[4]);
    uint32_t capture_id = read_u32(&packet[12]);
    uint8_t valid_mask = packet[16];

    int32_t latitude_e7 =
        (int32_t)read_u32(&packet[17]);

    int32_t longitude_e7 =
        (int32_t)read_u32(&packet[21]);

    float board_temperature =
        read_float(&packet[25]);

    float cow_temperature =
        read_float(&packet[29]);

    float acceleration_mean =
        read_float(&packet[33]);

    float acceleration_rms =
        read_float(&packet[37]);

    float microphone_rms =
        read_float(&packet[41]);

    uint32_t flags =
        read_u32(&packet[45]);

    printf("{"
           "\"type\":\"telemetry\","
           "\"device_id\":%llu,"
           "\"capture_id\":%lu,"
           "\"valid_mask\":%u,"
           "\"latitude\":%.7f,"
           "\"longitude\":%.7f,"
           "\"board_temperature_c\":%.3f,"
           "\"cow_temperature_c\":%.3f,"
           "\"accel_mean_g\":%.6f,"
           "\"accel_rms_g\":%.6f,"
           "\"mic_rms\":%.6f,"
           "\"flags\":%lu"
           "}\n",
           (unsigned long long)device_id,
           (unsigned long)capture_id,
           (unsigned)valid_mask,
           latitude_e7 / 10000000.0,
           longitude_e7 / 10000000.0,
           board_temperature,
           cow_temperature,
           acceleration_mean,
           acceleration_rms,
           microphone_rms,
           (unsigned long)flags);

    fflush(stdout);
}

static bool valid_features_packet(const uint8_t *packet,
                                  int length)
{
    if (length < FEATURES_HEADER_BYTES) {
        return false;
    }

    if (read_u16(&packet[0]) != TELEMETRY_MAGIC ||
        packet[2] != FEATURES_VERSION ||
        packet[3] != FEATURES_TYPE) {
        return false;
    }

    int blocks = packet[39];
    int seconds = packet[40];

    return length == FEATURES_HEADER_BYTES +
                     blocks * FEATURES_BLOCK_BYTES +
                     seconds;
}

// Prints one value, or null when its valid bit is not set.
static void print_temperature(const char *name,
                              const uint8_t *data,
                              bool valid)
{
    if (valid) {
        printf("\"%s\":%.2f,", name, (int16_t)read_u16(data) / 100.0);
    } else {
        printf("\"%s\":null,", name);
    }
}

static void print_features(const uint8_t *packet,
                           int length)
{
    uint8_t valid_mask = packet[16];
    bool gps_valid = (valid_mask & VALID_GPS) != 0;
    int blocks = packet[39];
    int seconds = packet[40];

    printf("{"
           "\"type\":\"features\","
           "\"device_id\":%llu,"
           "\"capture_id\":%lu,"
           "\"valid_mask\":%u,"
           "\"flags\":%lu,",
           (unsigned long long)read_u64(&packet[4]),
           (unsigned long)read_u32(&packet[12]),
           (unsigned)valid_mask,
           (unsigned long)read_u32(&packet[17]));

    if (gps_valid) {
        printf("\"latitude\":%.7f,\"longitude\":%.7f,",
               (int32_t)read_u32(&packet[21]) / 10000000.0,
               (int32_t)read_u32(&packet[25]) / 10000000.0);
    } else {
        printf("\"latitude\":null,\"longitude\":null,");
    }

    printf("\"gps_satellites\":%u,\"gps_fix_quality\":%u,",
           packet[29], packet[30]);

    print_temperature("board_temperature_c", &packet[31],
                      (valid_mask & VALID_BOARD) != 0);
    print_temperature("cow_temperature_c", &packet[33],
                      (valid_mask & VALID_COW) != 0);

    printf("\"capture_ms\":%u,"
           "\"block_ms\":%u,"
           "\"sound_events\":%u,",
           read_u16(&packet[35]),
           read_u16(&packet[37]),
           packet[41]);

    const uint8_t *block = &packet[FEATURES_HEADER_BYTES];
    const uint8_t *loudness = block + blocks * FEATURES_BLOCK_BYTES;

    // Same names as the database columns, so the Jetson bridge can copy them:
    //   mic_rms            = RMS over the recording, rebuilt from the per-second dB
    //   accel_*_derivative = average jerk |da/dt| of the blocks (g/s)
    if ((valid_mask & VALID_MIC) && seconds > 0) {
        double mean_square = 0;
        for (int i = 0; i < seconds; i++) {
            mean_square += pow(10.0, loudness[i] / 3.0 / 10.0);   // (10^(dB/20))^2
        }
        printf("\"mic_rms\":%.2f,", sqrt(mean_square / seconds));
    } else {
        printf("\"mic_rms\":null,");
    }

    static const char *const axis_names[3] = { "x", "y", "z" };
    for (int a = 0; a < 3; a++) {
        if ((valid_mask & VALID_ACCEL) && blocks > 0) {
            uint32_t sum = 0;
            for (int i = 0; i < blocks; i++) {
                sum += read_u16(&block[i * FEATURES_BLOCK_BYTES + 14 + 2 * a]);
            }
            printf("\"accel_%s_derivative\":%.3f,", axis_names[a], sum / 100.0 / blocks);
        } else {
            printf("\"accel_%s_derivative\":null,", axis_names[a]);
        }
    }

    // One object per block: tilt (posture), movement, jolts, sound bands,
    // and the acceleration derivative (jerk) per axis
    printf("\"blocks\":[");
    for (int i = 0; i < blocks; i++, block += FEATURES_BLOCK_BYTES) {
        printf("%s{"
               "\"tilt_x_mg\":%d,\"tilt_y_mg\":%d,\"tilt_z_mg\":%d,"
               "\"odba_mg\":%u,\"peak_mg\":%u,\"moves\":%u,"
               "\"sound_low_db\":%.1f,\"sound_mid_db\":%.1f,\"sound_high_db\":%.1f,"
               "\"jerk_x_gps\":%.2f,\"jerk_y_gps\":%.2f,\"jerk_z_gps\":%.2f"
               "}",
               i ? "," : "",
               (int16_t)read_u16(&block[0]),
               (int16_t)read_u16(&block[2]),
               (int16_t)read_u16(&block[4]),
               read_u16(&block[6]),
               read_u16(&block[8]),
               block[10],
               block[11] / 3.0,
               block[12] / 3.0,
               block[13] / 3.0,
               read_u16(&block[14]) / 100.0,
               read_u16(&block[16]) / 100.0,
               read_u16(&block[18]) / 100.0);
    }

    // Loudness per second (dB)
    printf("],\"loudness_db\":[");
    for (int i = 0; i < seconds; i++) {
        printf("%s%.1f", i ? "," : "", block[i] / 3.0);
    }
    printf("],\"length\":%d}\n", length);

    fflush(stdout);
}

void app_main(void)
{
    ESP_LOGI(TAG,
             "CowNect Heltec receiver starting");

    ESP_LOGI(TAG,
             "Frequency: %lu Hz | BW: %d kHz | SF: %d | CR: 4/%d",
             (unsigned long)LORA_FREQUENCY_HZ,
             LORA_BANDWIDTH_KHZ,
             LORA_SPREADING_FACTOR,
             LORA_CODING_RATE);

    if (!oled_init()) {
        ESP_LOGW(TAG,
                 "OLED initialization failed");
    }

    if (!lora_begin()) {
        ESP_LOGE(TAG,
                 "LoRa initialization failed");

        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    uint32_t packet_count = 0;
    uint64_t last_collar_id = 0;
    uint32_t last_capture_id = 0;
    int last_rssi = 0;
    float last_snr = 0.0f;
    uint32_t last_packet_time_ms = 0;

    oled_show_status(packet_count,
                 last_collar_id,
                 last_capture_id,
                 last_rssi,
                 last_snr,
                 0,
                 false);

    ESP_LOGI(TAG,
             "Listening for CowNect LoRa packets");

    uint8_t packet[LORA_MAX_PACKET];

    while (true) {
        int length = lora_receive(packet,
                                  sizeof(packet),
                                  1000);

        if (length <= 0) {
    uint32_t now_ms =
        xTaskGetTickCount() * portTICK_PERIOD_MS;

    uint32_t elapsed_seconds = 0;

    if (last_packet_time_ms != 0) {
        elapsed_seconds =
            (now_ms - last_packet_time_ms) / 1000;
    }

    oled_show_status(packet_count,
                     last_collar_id,
                     last_capture_id,
                     last_rssi,
                     last_snr,
                     elapsed_seconds,
                     last_packet_time_ms != 0);

    continue;
}

        // Collar status text (e.g. "GPS waiting, not fix yet"): "CM", version 1, type 0x12, then the text
        if (length > 4 && read_u16(&packet[0]) == TELEMETRY_MAGIC &&
            packet[2] == 1 && packet[3] == STATUS_TYPE) {
            printf("{\"type\":\"collar_status\",\"text\":\"");
            for (int i = 4; i < length; i++) {
                char ch = (char)packet[i];
                if (ch == '"' || ch == '\\') putchar('\\');
                putchar(ch >= 0x20 && ch < 0x7F ? ch : '?');
            }
            printf("\",\"rssi\":%.0f,\"snr\":%.1f}\n",
                   (double)lora_last_packet_rssi(), (double)lora_last_packet_snr());
            fflush(stdout);
            continue;
        }

        // LORA_FULL feature packet, or HYBRID 49-byte summary
        if (valid_features_packet(packet, length)) {
            print_features(packet, length);
        } else {
            print_telemetry(packet, length);

            if (!valid_telemetry_packet(packet, length)) {
                continue;
            }
        }

        packet_count++;
        last_collar_id = read_u64(&packet[4]);
        last_capture_id = read_u32(&packet[12]);
        last_rssi = lora_last_packet_rssi();
        last_snr = lora_last_packet_snr();

        last_packet_time_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

        oled_show_status(packet_count,
                 last_collar_id,
                 last_capture_id,
                 last_rssi,
                 last_snr,
                 0,
                 true);
    }
}