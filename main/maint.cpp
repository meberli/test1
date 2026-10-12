#include "maint.h"

#include <stdio.h>
#include <string.h>

#include "clock_sync.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_https_ota.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "settings.h"
#include "sys.h"

namespace {

httpd_handle_t g_server = nullptr;
volatile int g_ota_pct = -1;
volatile bool g_restart = false;

void restart_task(void*) {
  vTaskDelay(pdMS_TO_TICKS(1500));
  esp_restart();
}

void schedule_restart() {
  g_restart = true;
  xTaskCreatePinnedToCore(restart_task, "restart", 2048, nullptr, 1, nullptr, 0);
}

size_t status_json(char* out, size_t n) {
  MaintStatus s;
  maint_collect(&s);
  return snprintf(out, n,
                  "{\"version\":\"%s\",\"built\":\"%s\",\"idf\":\"%s\",\"partition\":\"%s\",\"uptime_s\":%lu,"
                  "\"wifi\":%s,\"ssid\":\"%s\",\"rssi\":%d,\"ip\":\"%s\",\"heap_internal\":%lu,"
                  "\"heap_internal_largest\":%lu,\"heap_psram\":%lu,\"ntp_synced\":%s,\"ntp_age_s\":%lu}",
                  s.version, s.built, s.idf, s.partition, static_cast<unsigned long>(s.uptime_s),
                  s.wifi ? "true" : "false", s.ssid, s.rssi, s.ip, static_cast<unsigned long>(s.free_internal),
                  static_cast<unsigned long>(s.largest_internal), static_cast<unsigned long>(s.free_psram),
                  s.ntp_synced ? "true" : "false", static_cast<unsigned long>(s.ntp_age_s));
}

const char kPage[] =
    "<!doctype html><html lang=de><head><meta charset=utf-8><meta name=viewport content='width=device-width'>"
    "<title>esp_display Wartung</title><style>body{font-family:system-ui,sans-serif;background:#0b0f1a;color:#e8ecf4;"
    "max-width:640px;margin:24px auto;padding:0 16px}section{background:#1b2333;border:1px solid #2a3448;"
    "border-radius:16px;padding:16px;margin:16px 0}a,button{color:#7cc4ff}button{background:#131926;border:1px solid "
    "#2a3448;border-radius:10px;padding:8px 14px;font-size:15px}pre{white-space:pre-wrap;font-size:13px}"
    "textarea{width:100%;height:120px;background:#131926;color:#e8ecf4;border-radius:8px}</style></head><body>"
    "<h2>esp_display Wartung</h2><section><h3>Status</h3><pre id=s>...</pre></section>"
    "<section><h3>Einstellungen sichern</h3><a href=/backup download=esp_display-settings.json>Backup (ohne "
    "Geheimnisse)</a> &middot; <a href='/backup?secrets=1' download=esp_display-settings-secrets.json>mit "
    "WLAN-Passwort, API-Key und Kalender-URL</a></section>"
    "<section><h3>Einstellungen wiederherstellen</h3><input type=file id=rf accept=.json> "
    "<button onclick=restore()>Wiederherstellen</button><pre id=rr></pre></section>"
    "<section><h3>Firmware-Update</h3><input type=file id=ff accept=.bin> <button onclick=upload()>Hochladen und "
    "installieren</button><pre id=fr></pre></section><script>"
    "fetch('/status').then(r=>r.json()).then(j=>s.textContent=JSON.stringify(j,null,1));"
    "async function restore(){const f=rf.files[0];if(!f)return;rr.textContent='...';const r=await fetch('/restore',"
    "{method:'POST',body:await f.text()});rr.textContent=await r.text()}"
    "async function upload(){const f=ff.files[0];if(!f)return;fr.textContent='Lade hoch ('+f.size+' Bytes) ...';"
    "const r=await fetch('/update',{method:'POST',body:f});fr.textContent=await r.text()}"
    "</script></body></html>";

esp_err_t on_root(httpd_req_t* req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, kPage, sizeof(kPage) - 1);
}

esp_err_t on_status(httpd_req_t* req) {
  char buf[512];
  size_t n = status_json(buf, sizeof(buf));
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, buf, n);
}

esp_err_t on_backup(httpd_req_t* req) {
  bool secrets = false;
  char q[32];
  if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
    char v[4];
    secrets = httpd_query_key_value(q, "secrets", v, sizeof(v)) == ESP_OK && v[0] == '1';
  }
  constexpr size_t kCap = 16 * 1024;
  char* buf = static_cast<char*>(heap_caps_malloc(kCap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (buf == nullptr) return httpd_resp_send_500(req);
  size_t n = settings_export_json(buf, kCap, secrets);
  console_printf("Web: settings backup (%u bytes, secrets=%d)\n", static_cast<unsigned>(n), secrets ? 1 : 0);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=esp_display-settings.json");
  esp_err_t e = httpd_resp_send(req, buf, n);
  heap_caps_free(buf);
  return e;
}

esp_err_t on_restore(httpd_req_t* req) {
  constexpr size_t kCap = 16 * 1024;
  if (req->content_len == 0 || req->content_len >= kCap) {
    httpd_resp_set_status(req, "400 Bad Request");
    return httpd_resp_sendstr(req, "Leere oder zu grosse Datei");
  }
  char* buf = static_cast<char*>(heap_caps_malloc(kCap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (buf == nullptr) return httpd_resp_send_500(req);
  size_t got = 0;
  while (got < req->content_len) {
    int r = httpd_req_recv(req, buf + got, req->content_len - got);
    if (r <= 0) {
      if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
      heap_caps_free(buf);
      return ESP_FAIL;
    }
    got += static_cast<size_t>(r);
  }
  int n = settings_import_json(buf, got);
  heap_caps_free(buf);
  console_printf("Web: settings restore -> %d keys\n", n);
  if (n < 0) {
    httpd_resp_set_status(req, "400 Bad Request");
    return httpd_resp_sendstr(req, "Keine esp_display-Einstellungsdatei");
  }
  char msg[80];
  snprintf(msg, sizeof(msg), "%d Einstellungen wiederhergestellt. Neustart ...", n);
  httpd_resp_sendstr(req, msg);
  schedule_restart();
  return ESP_OK;
}

esp_err_t on_update(httpd_req_t* req) {
  const esp_partition_t* part = esp_ota_get_next_update_partition(nullptr);
  if (part == nullptr || req->content_len == 0 || req->content_len > part->size) {
    httpd_resp_set_status(req, "400 Bad Request");
    return httpd_resp_sendstr(req, part == nullptr ? "Keine OTA-Partition" : "Falsche Dateigroesse");
  }
  esp_ota_handle_t ota = 0;
  if (esp_ota_begin(part, req->content_len, &ota) != ESP_OK) return httpd_resp_send_500(req);
  console_printf("Web: firmware upload %u bytes -> %s\n", static_cast<unsigned>(req->content_len), part->label);
  char* buf = static_cast<char*>(heap_caps_malloc(4096, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (buf == nullptr) {
    esp_ota_abort(ota);
    return httpd_resp_send_500(req);
  }
  size_t left = req->content_len;
  esp_err_t err = ESP_OK;
  g_ota_pct = 0;
  while (left > 0 && err == ESP_OK) {
    int r = httpd_req_recv(req, buf, left < 4096 ? left : 4096);
    if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
    if (r <= 0) {
      err = ESP_FAIL;
      break;
    }
    err = esp_ota_write(ota, buf, r);
    left -= static_cast<size_t>(r);
    g_ota_pct = static_cast<int>(100 - (left * 100) / req->content_len);
  }
  heap_caps_free(buf);
  if (err == ESP_OK) err = esp_ota_end(ota);
  else esp_ota_abort(ota);
  if (err == ESP_OK) err = esp_ota_set_boot_partition(part);
  g_ota_pct = -1;
  if (err != ESP_OK) {
    console_printf("Web: firmware upload failed: %s\n", esp_err_to_name(err));
    httpd_resp_set_status(req, "500 Internal Server Error");
    char msg[80];
    snprintf(msg, sizeof(msg), "Update fehlgeschlagen: %s", esp_err_to_name(err));
    return httpd_resp_sendstr(req, msg);
  }
  console_printf("Web: firmware installed in %s, restarting\n", part->label);
  httpd_resp_sendstr(req, "Firmware installiert. Neustart ...");
  schedule_restart();
  return ESP_OK;
}

}  // namespace

void maint_collect(MaintStatus* s) {
  memset(s, 0, sizeof(*s));
  wifi_ap_record_t ap;
  if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
    s->wifi = true;
    s->rssi = ap.rssi;
    snprintf(s->ssid, sizeof(s->ssid), "%s", reinterpret_cast<const char*>(ap.ssid));
  }
  esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  esp_netif_ip_info_t ip = {};
  if (netif != nullptr && esp_netif_get_ip_info(netif, &ip) == ESP_OK) {
    snprintf(s->ip, sizeof(s->ip), IPSTR, IP2STR(&ip.ip));
  }
  s->uptime_s = static_cast<uint32_t>(esp_timer_get_time() / 1000000);
  s->free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  s->largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  s->free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  const esp_app_desc_t* d = esp_app_get_description();
  snprintf(s->version, sizeof(s->version), "%s", d->version);
  snprintf(s->built, sizeof(s->built), "%.11s %.5s", d->date, d->time);
  snprintf(s->idf, sizeof(s->idf), "%s", d->idf_ver);
  const esp_partition_t* run = esp_ota_get_running_partition();
  snprintf(s->partition, sizeof(s->partition), "%s", run != nullptr ? run->label : "?");
  s->ntp_synced = clock_synced();
  unsigned last = clock_last_sync_s();
  s->ntp_age_s = last > 0 && s->uptime_s >= last ? s->uptime_s - last : 0;
  s->web = g_server != nullptr;
}

void maint_mark_valid() {
  const esp_partition_t* run = esp_ota_get_running_partition();
  esp_ota_img_states_t st;
  if (run != nullptr && esp_ota_get_state_partition(run, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) {
    esp_ota_mark_app_valid_cancel_rollback();
    console_puts("OTA: new image marked valid");
  }
}

int maint_ota_progress() { return g_ota_pct; }

bool maint_ota_from_url(const char* url, char* err, size_t n) {
  esp_http_client_config_t http = {};
  http.url = url;
  http.timeout_ms = 20000;
  http.buffer_size = 4096;
  http.buffer_size_tx = 2048;  // GitHub redirects to long signed URLs
  http.keep_alive_enable = true;
  if (strncmp(url, "https://", 8) == 0) http.crt_bundle_attach = esp_crt_bundle_attach;
  esp_https_ota_config_t cfg = {};
  cfg.http_config = &http;
  esp_https_ota_handle_t h = nullptr;
  g_ota_pct = 0;
  esp_err_t e = esp_https_ota_begin(&cfg, &h);
  if (e != ESP_OK) {
    snprintf(err, n, "Download: %s", esp_err_to_name(e));
    g_ota_pct = -1;
    return false;
  }
  int total = esp_https_ota_get_image_size(h);
  for (;;) {
    e = esp_https_ota_perform(h);
    if (e != ESP_ERR_HTTPS_OTA_IN_PROGRESS) break;
    int got = esp_https_ota_get_image_len_read(h);
    if (total > 0) g_ota_pct = got * 100 / total;
  }
  if (e == ESP_OK && !esp_https_ota_is_complete_data_received(h)) e = ESP_ERR_INVALID_SIZE;
  if (e != ESP_OK) {
    esp_https_ota_abort(h);
    snprintf(err, n, "Update: %s", esp_err_to_name(e));
    g_ota_pct = -1;
    return false;
  }
  e = esp_https_ota_finish(h);
  g_ota_pct = -1;
  if (e != ESP_OK) {
    snprintf(err, n, "Abschluss: %s", esp_err_to_name(e));
    return false;
  }
  return true;
}

bool maint_web_start() {
  if (g_server != nullptr) return true;
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.core_id = 0;  // network work on core 0
  cfg.stack_size = 8192;
  cfg.lru_purge_enable = true;
  if (httpd_start(&g_server, &cfg) != ESP_OK) {
    g_server = nullptr;
    return false;
  }
  const httpd_uri_t uris[] = {
      {"/", HTTP_GET, on_root, nullptr},
      {"/status", HTTP_GET, on_status, nullptr},
      {"/backup", HTTP_GET, on_backup, nullptr},
      {"/restore", HTTP_POST, on_restore, nullptr},
      {"/update", HTTP_POST, on_update, nullptr},
  };
  for (const httpd_uri_t& u : uris) httpd_register_uri_handler(g_server, &u);
  console_puts("Web: maintenance server on port 80");
  return true;
}

void maint_web_stop() {
  if (g_server == nullptr) return;
  httpd_stop(g_server);
  g_server = nullptr;
  console_puts("Web: maintenance server stopped");
}

bool maint_web_running() { return g_server != nullptr; }
bool maint_restart_pending() { return g_restart; }
