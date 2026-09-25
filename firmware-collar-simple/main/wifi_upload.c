// wifi_upload.c - Wi-Fi station + one TCP connection per capture.
#include "wifi_upload.h"
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "capture.h"
#include "data_format.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "lwip/sockets.h"
#include "nvs_flash.h"

static const char *TAG = "wifi";

#define GOT_IP_BIT BIT0
static EventGroupHandle_t s_events;
static bool s_initialized;

// Checks SSID, Jetson IP/port and both timeouts (see wifi_upload.h).
bool wifi_config_ready(void)
{
    const char *missing = NULL;
    if (strlen(WIFI_SSID) == 0) missing = "WIFI_SSID";
    else if (strlen(JETSON_IP) == 0) missing = "JETSON_IP";
    else if (JETSON_RAW_PORT == 0) missing = "JETSON_RAW_PORT";
    else if (WIFI_CONNECT_TIMEOUT_MS == 0) missing = "WIFI_CONNECT_TIMEOUT_MS";
    else if (TCP_SEND_TIMEOUT_MS == 0) missing = "TCP_SEND_TIMEOUT_MS";
    if (missing) {
        ESP_LOGW(TAG, "Wi-Fi upload skipped: %s is not set in wifi_upload.h", missing);
        return false;
    }
    return true;
}

// Wi-Fi/IP event handler: when the router gives us an IP address, set GOT_IP_BIT
// so wifi_upload_capture() can stop waiting.
static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) xEventGroupSetBits(s_events, GOT_IP_BIT);
}

// One-time Wi-Fi set-up after boot: NVS flash (Wi-Fi needs it), network
// interface, event loop, Wi-Fi driver in station mode with SSID/password.
static bool wifi_init_once(void)
{
    if (s_initialized) return true;
    esp_err_t err = nvs_flash_init();                  // Wi-Fi stores calibration data in NVS
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) return false;
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&init) != ESP_OK) return false;
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL);
    s_events = xEventGroupCreate();

    wifi_config_t cfg = {0};
    strncpy((char *)cfg.sta.ssid, WIFI_SSID, sizeof(cfg.sta.ssid));
    strncpy((char *)cfg.sta.password, WIFI_PASSWORD, sizeof(cfg.sta.password));
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &cfg);
    s_initialized = true;
    return true;
}

// Keeps calling send() until all len bytes are out. False on error or timeout.
static bool send_all(int sock, const void *data, size_t len)
{
    const uint8_t *p = data;
    while (len > 0) {
        int n = send(sock, p, len, 0);
        if (n <= 0) return false;
        p += n;
        len -= (size_t)n;
    }
    return true;
}

// Opens a TCP connection to the Jetson and sends, in order: the 21-byte upload
// header, the stream head, the microphone samples straight from PSRAM, and the
// 12-byte tail. Closes the connection afterwards.
static bool send_capture(const capture_t *c)
{
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) return false;
    struct timeval tv = { .tv_sec = TCP_SEND_TIMEOUT_MS / 1000, .tv_usec = (TCP_SEND_TIMEOUT_MS % 1000) * 1000 };
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons(JETSON_RAW_PORT) };
    inet_pton(AF_INET, JETSON_IP, &addr.sin_addr);
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        ESP_LOGE(TAG, "cannot connect to %s:%d", JETSON_IP, JETSON_RAW_PORT);
        close(sock);
        return false;
    }

    uint8_t *head = malloc(STREAM_HEAD_MAX);
    bool ok = head != NULL;
    if (ok) {
        uint32_t total = stream_size(c);
        uint8_t hdr[UPLOAD_HEADER_BYTES], tail[STREAM_TAIL_BYTES];
        size_t hdr_len = build_upload_header(c, total, hdr);
        size_t head_len = build_stream_head(c, head);
        size_t tail_len = build_stream_tail(tail);
        // The ESP32 is little-endian, so the uint16 microphone buffer already has the
        // byte order of the stream format and can be sent as it is.
        ok = send_all(sock, hdr, hdr_len) && send_all(sock, head, head_len) &&
             send_all(sock, c->analog.mic, c->analog.mic_count * sizeof(uint16_t)) && send_all(sock, tail, tail_len);
        if (ok) ESP_LOGI(TAG, "uploaded capture %u: %u bytes", (unsigned)c->capture_id, (unsigned)total);
        else ESP_LOGE(TAG, "upload interrupted");
        free(head);
    }
    shutdown(sock, SHUT_RDWR);
    close(sock);
    return ok;
}

// See wifi_upload.h: init (first time) -> start + connect -> wait for IP ->
// send_capture() -> disconnect + stop.
bool wifi_upload_capture(const capture_t *c)
{
    if (!wifi_config_ready()) return false;
    if (!wifi_init_once()) {
        ESP_LOGE(TAG, "Wi-Fi init failed");
        return false;
    }
    xEventGroupClearBits(s_events, GOT_IP_BIT);
    esp_wifi_start();
    esp_wifi_connect();
    bool ok = false;
    EventBits_t bits = xEventGroupWaitBits(s_events, GOT_IP_BIT, pdFALSE, pdTRUE,
                                           pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));
    if (bits & GOT_IP_BIT) ok = send_capture(c);
    else ESP_LOGE(TAG, "could not connect to \"%s\" in time", WIFI_SSID);
    esp_wifi_disconnect();
    esp_wifi_stop();
    return ok;
}
