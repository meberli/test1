#pragma once
// Wi-Fi station and a small blocking HTTP GET over esp_http_client.

#include <stddef.h>
#include <stdint.h>

#include "settings.h"

void wifi_start(const char* ssid, const char* password, const char* hostname);
bool wifi_connected();
// true once the driver reported a final failure (wrong password, no AP).
bool wifi_failed();
int wifi_auth_fails();   // auth / 4-way handshake rejections since wifi_start
int wifi_noap_fails();   // "no AP found" since wifi_start
int wifi_last_reason();  // last WIFI_REASON_* of a disconnect
// Blocks (event group, no polling) until connected, a disconnect event arrives
// or ms elapse. Returns wifi_connected(). Network task only.
bool wifi_wait(uint32_t ms);
// Blocking scan (call from the network task). Returns the number of APs,
// strongest first, duplicates merged.
int wifi_scan(WifiAp* out, int cap);
// SSID of the configured network.
const char* wifi_ssid();

struct HttpGetResult {
  bool ok;         // transport ok and the whole body fit into the buffer
  int status;      // HTTP status, 0 if no response
  size_t length;   // body bytes stored
  char error[64];  // transport error text when !ok
};

// GET url into body (cap bytes). user != nullptr adds HTTP basic auth.
// HTTPS uses the IDF certificate bundle.
// hdr_name/hdr_value add one request header (e.g. x-windy-api-key).
HttpGetResult http_get(const char* url, const char* user, const char* password, uint8_t* body, size_t cap,
                       int timeout_ms, const char* hdr_name = nullptr, const char* hdr_value = nullptr);
