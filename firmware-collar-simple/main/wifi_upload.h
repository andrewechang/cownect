// wifi_upload.h - connect to Wi-Fi and send the raw capture to the Jetson over TCP.
//
// This is a plain TCP connection (not HTTP): the collar connects to the Jetson's
// IP and port, sends a 21-byte header + the capture stream, and closes.
// Used only when COMM_MODE = COMM_HYBRID (config.h).
#pragma once
#include <stdbool.h>
#include "config.h"
#include "capture.h"

// ================================================================ SETTINGS
#define WIFI_SSID              ""      // TODO network name
#define WIFI_PASSWORD          ""      // TODO network password
#define JETSON_IP              ""      // TODO e.g. "192.168.1.50"
#define JETSON_RAW_PORT        0       // TODO TCP port the Jetson receiver listens on
#define WIFI_CONNECT_TIMEOUT_MS 0      // TODO e.g. 10000 - 0 = not set
#define TCP_SEND_TIMEOUT_MS    0       // TODO e.g. 5000 - 0 = not set
// ================================================================


// Returns true when every setting above is filled in; otherwise logs the first
// missing one and returns false.
bool wifi_config_ready(void);

// Starts Wi-Fi, waits for an IP address, sends [upload header][capture stream]
// to JETSON_IP:JETSON_RAW_PORT, then disconnects and turns Wi-Fi off.
// Returns true if every byte was sent.
bool wifi_upload_capture(const capture_t *c);
