// wifi_upload.h - connect to Wi-Fi and send the raw capture, either to the
// Jetson over plain TCP or to a website over HTTPS.
//
// Both targets get exactly the same bytes: a 21-byte upload header + the capture
// stream (docs/protocols.md). Only the transport is different:
//   UPLOAD_TO_JETSON : plain TCP (not HTTP) to JETSON_IP:JETSON_RAW_PORT, then close
//   UPLOAD_TO_WEB    : one HTTPS POST to WEB_UPLOAD_URL, body = those bytes
//                      (Content-Type application/octet-stream), API key in a header.
//                      2xx reply = success. The server certificate is checked
//                      against ESP-IDF's built-in list of trusted CAs.
// Used only when COMM_MODE = COMM_HYBRID (config.h).
#pragma once
#include <stdbool.h>
#include "config.h"
#include "capture.h"

// ================================================================ SETTINGS
#define UPLOAD_TO_JETSON       1
#define UPLOAD_TO_WEB          2
#define UPLOAD_TARGET          UPLOAD_TO_JETSON

#define WIFI_SSID              ""      // TODO network name
#define WIFI_PASSWORD          ""      // TODO network password
#define WIFI_CONNECT_TIMEOUT_MS 10000  // wait this long for an IP address from the router
#define TCP_SEND_TIMEOUT_MS    10000   // a send (Jetson) or HTTPS read/write (website) may stall this long

// --- UPLOAD_TO_JETSON ---
#define JETSON_IP              ""      // TODO e.g. "192.168.1.50" (numbers only, no names)
#define JETSON_RAW_PORT        0       // TODO TCP port the Jetson receiver listens on

// --- UPLOAD_TO_WEB ---
// Full address, must start with "https://". The port goes in the URL only if it is
// not 443, e.g. "https://203.0.113.7:8443/upload".
#define WEB_UPLOAD_URL         ""      // TODO e.g. "https://example.com/api/upload"
// Header the website reads the key from. For "Authorization: Bearer <key>" use
// WEB_API_KEY_HEADER "Authorization" and WEB_API_KEY "Bearer <key>".
#define WEB_API_KEY_HEADER     "X-API-Key"   // TODO change to what the website expects
#define WEB_API_KEY            ""      // TODO the key - do NOT push the real key to GitHub
// ================================================================


// Returns true when every setting the chosen UPLOAD_TARGET needs is filled in;
// otherwise logs the first missing one and returns false.
bool wifi_config_ready(void);

// Starts Wi-Fi, waits for an IP address, sends [upload header][capture stream]
// to the chosen target, then disconnects and turns Wi-Fi off.
// Returns true if every byte was sent (website: and it replied 2xx).
bool wifi_upload_capture(const capture_t *c);

// Bring-up test (test_mode.c): connects to Wi-Fi, logs the IP address and signal
// strength, then checks the target without sending any data:
//   Jetson  : opens and closes a TCP connection
//   website : looks up the name and completes the HTTPS handshake (certificate
//             checked); the API key is NOT checked, only a real upload does that
// then turns Wi-Fi off. Returns true if both steps worked.
// Call wifi_config_ready() first.
bool wifi_test_connection(void);
