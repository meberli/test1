#pragma once
// Wi-Fi station and a small blocking HTTP GET over esp_http_client.

#include <stddef.h>
#include <stdint.h>

#include "settings.h"

void wifi_start(const char* ssid, const char* password, const char* hostname);
bool wifi_connected();
// true once the driver reported a final failure (wrong password, no AP).
bool wifi_failed();
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
HttpGetResult http_get(const char* url, const char* user, const char* password, uint8_t* body, size_t cap,
                       int timeout_ms);
