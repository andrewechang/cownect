// wifi_upload.c - Wi-Fi station + one upload per capture (Jetson TCP or website HTTPS).
#include "wifi_upload.h"
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "capture.h"
#include "data_format.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_tls.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "lwip/sockets.h"
#include "nvs_flash.h"

static const char *TAG = "wifi";

#define GOT_IP_BIT BIT0
static EventGroupHandle_t s_events;
static bool s_initialized;

// Checks SSID, the chosen target's settings and both timeouts (see wifi_upload.h).
bool wifi_config_ready(void)
{
    const char *missing = NULL;
    if (strlen(WIFI_SSID) == 0) missing = "WIFI_SSID";
#if UPLOAD_TARGET == UPLOAD_TO_WEB
    else if (strlen(WEB_UPLOAD_URL) == 0) missing = "WEB_UPLOAD_URL";
    else if (strlen(WEB_API_KEY_HEADER) == 0) missing = "WEB_API_KEY_HEADER";
    else if (strlen(WEB_API_KEY) == 0) missing = "WEB_API_KEY";
#else
    else if (strlen(JETSON_IP) == 0) missing = "JETSON_IP";
    else if (JETSON_RAW_PORT == 0) missing = "JETSON_RAW_PORT";
#endif
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

// Writes len bytes to the open connection `ctx`. False on error or timeout.
typedef bool (*writer_t)(void *ctx, const void *data, size_t len);

// Bytes in one upload: the 21-byte upload header + the capture stream (incl. its tail).
static uint32_t upload_size(const capture_t *c)
{
    return UPLOAD_HEADER_BYTES + stream_size(c);
}

// Builds the upload and passes it to `write`, in order: the 21-byte upload header,
// the stream head, the microphone samples straight from PSRAM, and the 12-byte tail.
// Both targets get exactly these bytes.
static bool write_capture(const capture_t *c, writer_t write, void *ctx)
{
    uint8_t *head = malloc(STREAM_HEAD_MAX);
    if (!head) {
        ESP_LOGE(TAG, "no memory for the stream head");
        return false;
    }
    uint8_t hdr[UPLOAD_HEADER_BYTES], tail[STREAM_TAIL_BYTES];
    size_t hdr_len = build_upload_header(c, stream_size(c), hdr);
    size_t head_len = build_stream_head(c, head);
    size_t tail_len = build_stream_tail(tail);
    // The ESP32 is little-endian, so the uint16 microphone buffer already has the
    // byte order of the stream format and can be sent as it is.
    bool ok = write(ctx, hdr, hdr_len) && write(ctx, head, head_len) &&
              write(ctx, c->analog.mic, c->analog.mic_count * sizeof(uint16_t)) && write(ctx, tail, tail_len);
    free(head);
    return ok;
}

// ---------------------------------------------------------------- Jetson (plain TCP)
// Keeps calling send() on the socket *ctx until all len bytes are out.
static bool tcp_write(void *ctx, const void *data, size_t len)
{
    int sock = *(int *)ctx;
    const uint8_t *p = data;
    while (len > 0) {
        int n = send(sock, p, len, 0);
        if (n <= 0) return false;
        p += n;
        len -= (size_t)n;
    }
    return true;
}

// Opens a TCP connection to JETSON_IP:JETSON_RAW_PORT with the send timeout set.
// Returns the socket, or -1 (and logs) if the Jetson does not accept the connection.
static int open_jetson_socket(void)
{
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) return -1;
    struct timeval tv = { .tv_sec = TCP_SEND_TIMEOUT_MS / 1000, .tv_usec = (TCP_SEND_TIMEOUT_MS % 1000) * 1000 };
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons(JETSON_RAW_PORT) };
    inet_pton(AF_INET, JETSON_IP, &addr.sin_addr);
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        ESP_LOGE(TAG, "cannot connect to %s:%d", JETSON_IP, JETSON_RAW_PORT);
        close(sock);
        return -1;
    }
    return sock;
}

// Opens a TCP connection to the Jetson, sends the upload and closes the connection.
static bool send_to_jetson(const capture_t *c)
{
    int sock = open_jetson_socket();
    if (sock < 0) return false;
    bool ok = write_capture(c, tcp_write, &sock);
    if (ok) ESP_LOGI(TAG, "uploaded capture %u to the Jetson: %u bytes", (unsigned)c->capture_id,
                     (unsigned)upload_size(c));
    else ESP_LOGE(TAG, "upload interrupted");
    shutdown(sock, SHUT_RDWR);
    close(sock);
    return ok;
}

// ---------------------------------------------------------------- website (HTTPS)
// Keeps calling esp_http_client_write() on the client `ctx` until all len bytes are out.
static bool https_write(void *ctx, const void *data, size_t len)
{
    const char *p = data;
    while (len > 0) {
        int n = esp_http_client_write((esp_http_client_handle_t)ctx, p, (int)len);
        if (n <= 0) return false;
        p += n;
        len -= (size_t)n;
    }
    return true;
}

// POSTs the upload to WEB_UPLOAD_URL with the API key header and checks the reply:
// 2xx = stored. On any other reply, logs the status and the start of the website's answer.
static bool send_to_web(const capture_t *c)
{
    esp_http_client_config_t cfg = {
        .url = WEB_UPLOAD_URL, .method = HTTP_METHOD_POST, .timeout_ms = TCP_SEND_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,          // check the certificate against trusted CAs
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        ESP_LOGE(TAG, "HTTPS client could not be created");
        return false;
    }
    esp_http_client_set_header(client, "Content-Type", "application/octet-stream");
    esp_http_client_set_header(client, WEB_API_KEY_HEADER, WEB_API_KEY);

    bool ok = false;
    esp_err_t err = esp_http_client_open(client, (int)upload_size(c));   // connect + handshake + headers
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "cannot connect to %s: %s", WEB_UPLOAD_URL, esp_err_to_name(err));
    } else if (!write_capture(c, https_write, client)) {
        ESP_LOGE(TAG, "upload interrupted");
    } else {
        esp_http_client_fetch_headers(client);
        int status = esp_http_client_get_status_code(client);
        ok = status >= 200 && status < 300;
        if (ok) {
            ESP_LOGI(TAG, "uploaded capture %u to the website: %u bytes, HTTP %d", (unsigned)c->capture_id,
                     (unsigned)upload_size(c), status);
        } else {
            char reply[128] = {0};
            int n = esp_http_client_read(client, reply, sizeof(reply) - 1);
            ESP_LOGE(TAG, "website did not accept the upload: HTTP %d %s", status, n > 0 ? reply : "");
            if (status == 401 || status == 403) ESP_LOGE(TAG, "-> check WEB_API_KEY and WEB_API_KEY_HEADER");
            if (status == 413) ESP_LOGE(TAG, "-> the website's upload size limit is below %u bytes",
                                        (unsigned)upload_size(c));
        }
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ok;
}

// Looks up the website, opens a connection and completes the HTTPS handshake
// (certificate checked), then closes it. Nothing is sent, so the API key is not checked.
static bool web_handshake(void)
{
    esp_tls_cfg_t cfg = { .crt_bundle_attach = esp_crt_bundle_attach, .timeout_ms = TCP_SEND_TIMEOUT_MS };
    esp_tls_t *tls = esp_tls_init();
    if (!tls) return false;
    bool ok = esp_tls_conn_http_new_sync(WEB_UPLOAD_URL, &cfg, tls) == 1;
    if (ok) ESP_LOGI(TAG, "website %s: HTTPS handshake OK, certificate trusted (nothing sent, API key not checked)",
                     WEB_UPLOAD_URL);
    else ESP_LOGE(TAG, "cannot reach %s over HTTPS: name lookup, connection or certificate failed (see esp-tls lines)",
                  WEB_UPLOAD_URL);
    esp_tls_conn_destroy(tls);
    return ok;
}

// ---------------------------------------------------------------- Wi-Fi
// Init (first time) -> start + connect -> wait up to WIFI_CONNECT_TIMEOUT_MS for
// an IP address. Returns true once the router has given us an IP address.
static bool wifi_connect(void)
{
    if (!wifi_init_once()) {
        ESP_LOGE(TAG, "Wi-Fi init failed");
        return false;
    }
    xEventGroupClearBits(s_events, GOT_IP_BIT);
    esp_wifi_start();
    esp_wifi_connect();
    EventBits_t bits = xEventGroupWaitBits(s_events, GOT_IP_BIT, pdFALSE, pdTRUE,
                                           pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));
    if (bits & GOT_IP_BIT) return true;
    ESP_LOGE(TAG, "could not connect to \"%s\" in time", WIFI_SSID);
    return false;
}

// Disconnects and switches the Wi-Fi radio off (safe to call if it never started).
static void wifi_off(void)
{
    esp_wifi_disconnect();
    esp_wifi_stop();
}

// See wifi_upload.h: wifi_connect() -> send to the chosen target -> wifi_off().
bool wifi_upload_capture(const capture_t *c)
{
    if (!wifi_config_ready()) return false;
    bool ok = wifi_connect() && (UPLOAD_TARGET == UPLOAD_TO_WEB ? send_to_web(c) : send_to_jetson(c));
    wifi_off();
    return ok;
}

// See wifi_upload.h: wifi_connect() -> log IP and signal -> check the target
// without sending anything -> wifi_off().
bool wifi_test_connection(void)
{
    bool ok = false;
    if (wifi_connect()) {
        esp_netif_ip_info_t ip = {0};
        esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), &ip);
        wifi_ap_record_t ap = {0};
        esp_wifi_sta_get_ap_info(&ap);
        ESP_LOGI(TAG, "connected to \"%s\": IP " IPSTR ", signal %d dBm, channel %d",
                 WIFI_SSID, IP2STR(&ip.ip), ap.rssi, ap.primary);
        if (UPLOAD_TARGET == UPLOAD_TO_WEB) {
            ok = web_handshake();
        } else {
            int sock = open_jetson_socket();
            if (sock >= 0) {
                ESP_LOGI(TAG, "Jetson %s:%d accepted the TCP connection (nothing sent)", JETSON_IP, JETSON_RAW_PORT);
                close(sock);
                ok = true;
            }
        }
    }
    wifi_off();
    return ok;
}
