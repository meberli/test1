#include "net.h"

#include <stdio.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_netif.h"
#include "esp_wifi.h"

namespace {
volatile bool g_connected = false;
volatile bool g_failed = false;
esp_netif_t* g_netif = nullptr;
char g_ssid[33] = "";

void on_event(void*, esp_event_base_t base, int32_t id, void* data) {
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    g_connected = false;
    auto* ev = static_cast<wifi_event_sta_disconnected_t*>(data);
    if (ev != nullptr && (ev->reason == WIFI_REASON_NO_AP_FOUND || ev->reason == WIFI_REASON_AUTH_FAIL ||
                          ev->reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT)) {
      g_failed = true;
    }
    esp_wifi_connect();  // keep trying; the UI decides when to give up
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    g_connected = true;
    g_failed = false;
  }
}
}  // namespace

void wifi_start(const char* ssid, const char* password, const char* hostname) {
  static bool inited = false;
  if (!inited) {
    g_netif = esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, nullptr);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, nullptr);
    if (hostname != nullptr) esp_netif_set_hostname(g_netif, hostname);
    inited = true;
  } else {
    esp_wifi_stop();
  }
  g_connected = false;
  g_failed = false;
  snprintf(g_ssid, sizeof(g_ssid), "%s", ssid);
  wifi_config_t wc = {};
  snprintf(reinterpret_cast<char*>(wc.sta.ssid), sizeof(wc.sta.ssid), "%s", ssid);
  snprintf(reinterpret_cast<char*>(wc.sta.password), sizeof(wc.sta.password), "%s", password);
  wc.sta.threshold.authmode = password[0] != '\0' ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
  esp_wifi_set_mode(WIFI_MODE_STA);
  esp_wifi_set_config(WIFI_IF_STA, &wc);
  esp_wifi_set_ps(WIFI_PS_NONE);  // was WiFi.setSleep(false)
  esp_wifi_start();
}

bool wifi_connected() { return g_connected; }
bool wifi_failed() { return g_failed; }

HttpGetResult http_get(const char* url, const char* user, const char* password, uint8_t* body, size_t cap,
                       int timeout_ms) {
  HttpGetResult r = {};
  esp_http_client_config_t cfg = {};
  cfg.url = url;
  cfg.timeout_ms = timeout_ms;
  cfg.buffer_size = 2048;
  cfg.disable_auto_redirect = true;
  if (strncmp(url, "https://", 8) == 0) {
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
  }
  if (user != nullptr) {
    cfg.username = user;
    cfg.password = password != nullptr ? password : "";
    cfg.auth_type = HTTP_AUTH_TYPE_BASIC;
  }
  esp_http_client_handle_t c = esp_http_client_init(&cfg);
  if (c == nullptr) {
    snprintf(r.error, sizeof(r.error), "client init failed");
    return r;
  }
  esp_err_t err = esp_http_client_open(c, 0);
  if (err != ESP_OK) {
    snprintf(r.error, sizeof(r.error), "%s", esp_err_to_name(err));
    esp_http_client_cleanup(c);
    return r;
  }
  int64_t content_length = esp_http_client_fetch_headers(c);
  r.status = esp_http_client_get_status_code(c);
  if (content_length > 0 && static_cast<size_t>(content_length) > cap) {
    snprintf(r.error, sizeof(r.error), "Response is %lld bytes (limit %u)", content_length,
             static_cast<unsigned>(cap));
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    return r;
  }
  size_t total = 0;
  bool overflow = false;
  for (;;) {
    if (total >= cap) {
      char dummy;
      if (esp_http_client_read(c, &dummy, 1) > 0) overflow = true;
      break;
    }
    int n = esp_http_client_read(c, reinterpret_cast<char*>(body + total), cap - total);
    if (n < 0) {
      snprintf(r.error, sizeof(r.error), "read error");
      esp_http_client_close(c);
      esp_http_client_cleanup(c);
      return r;
    }
    if (n == 0) {
      if (esp_http_client_is_complete_data_received(c) || content_length <= 0) break;
      // no more data although incomplete: connection closed or timeout
      break;
    }
    total += static_cast<size_t>(n);
  }
  esp_http_client_close(c);
  esp_http_client_cleanup(c);
  r.length = total;
  if (overflow) {
    snprintf(r.error, sizeof(r.error), "Response exceeded %u bytes", static_cast<unsigned>(cap));
    return r;
  }
  r.ok = true;
  return r;
}

const char* wifi_ssid() { return g_ssid; }

int wifi_scan(WifiAp* out, int cap) {
  wifi_scan_config_t sc = {};
  sc.show_hidden = false;
  if (esp_wifi_scan_start(&sc, true) != ESP_OK) {
    return 0;
  }
  uint16_t num = 32;
  static wifi_ap_record_t recs[32];
  if (esp_wifi_scan_get_ap_records(&num, recs) != ESP_OK) {
    return 0;
  }
  int n = 0;
  for (int i = 0; i < num && n < cap; ++i) {  // records come sorted by RSSI
    const char* ssid = reinterpret_cast<const char*>(recs[i].ssid);
    if (ssid[0] == '\0') continue;
    bool dup = false;
    for (int j = 0; j < n; ++j) {
      if (strcmp(out[j].ssid, ssid) == 0) dup = true;
    }
    if (dup) continue;
    snprintf(out[n].ssid, sizeof(out[n].ssid), "%s", ssid);
    out[n].rssi = recs[i].rssi;
    out[n].secure = recs[i].authmode != WIFI_AUTH_OPEN;
    n++;
  }
  return n;
}
