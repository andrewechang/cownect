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

#define TELEMETRY_BYTES    49
#define TELEMETRY_MAGIC    0x4D43
#define TELEMETRY_VERSION  1
#define TELEMETRY_TYPE     0x10

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

    oled_show_status(packet_count,
                     last_collar_id,
                     last_capture_id,
                     last_rssi,
                     last_snr,
                     false);

    ESP_LOGI(TAG,
             "Listening for CowNect LoRa packets");

    uint8_t packet[LORA_MAX_PACKET];

    while (true) {
        int length = lora_receive(packet,
                                  sizeof(packet),
                                  1000);

        if (length <= 0) {
            continue;
        }

        print_telemetry(packet, length);

        if (!valid_telemetry_packet(packet, length)) {
            continue;
        }

        if (length <= 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        packet_count++;
        last_collar_id = read_u64(&packet[4]);
        last_capture_id = read_u32(&packet[12]);
        last_rssi = lora_last_packet_rssi();
        last_snr = lora_last_packet_snr();

        oled_show_status(packet_count,
                         last_collar_id,
                         last_capture_id,
                         last_rssi,
                         last_snr,
                         true);
    }
}