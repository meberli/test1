#include "settings.h"

#include <stdio.h>
#include <string.h>

#include "config_load.h"
#include "nvs.h"
#include "sdkconfig.h"

namespace {
constexpr const char* kNs = "panel";

bool get_str(nvs_handle_t h, const char* key, char* out, size_t n) {
  size_t len = n;
  return nvs_get_str(h, key, out, &len) == ESP_OK;
}
}  // namespace

Location settings_default_location() {
  Location l = {};
  snprintf(l.name, sizeof(l.name), "Buchs SG");
  l.lat = 47.1674f;
  l.lon = 9.4779f;
  return l;
}

bool settings_load_wifi(WifiCreds* out) {
  memset(out, 0, sizeof(*out));
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READONLY, &h) == ESP_OK) {
    bool ok = get_str(h, "wifi_ssid", out->ssid, sizeof(out->ssid)) && out->ssid[0] != '\0';
    if (ok) get_str(h, "wifi_pass", out->pass, sizeof(out->pass));
    nvs_close(h);
    if (ok) return true;
  }
  snprintf(out->ssid, sizeof(out->ssid), "%s", WIFI_SSID);
  snprintf(out->pass, sizeof(out->pass), "%s", WIFI_PASSWORD);
  return out->ssid[0] != '\0';
}

bool settings_save_wifi(const WifiCreds& c) {
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READWRITE, &h) != ESP_OK) return false;
  bool ok = nvs_set_str(h, "wifi_ssid", c.ssid) == ESP_OK && nvs_set_str(h, "wifi_pass", c.pass) == ESP_OK &&
            nvs_commit(h) == ESP_OK;
  nvs_close(h);
  return ok;
}

Location settings_load_location() {
  Location l = settings_default_location();
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READONLY, &h) != ESP_OK) return l;
  Location n = {};
  size_t sz = sizeof(float);
  float lat = 0, lon = 0;
  bool ok = get_str(h, "loc_name", n.name, sizeof(n.name)) && nvs_get_blob(h, "loc_lat", &lat, &sz) == ESP_OK &&
            (sz = sizeof(float), nvs_get_blob(h, "loc_lon", &lon, &sz) == ESP_OK);
  nvs_close(h);
  if (!ok) return l;
  n.lat = lat;
  n.lon = lon;
  return n;
}

bool settings_save_location(const Location& l) {
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READWRITE, &h) != ESP_OK) return false;
  bool ok = nvs_set_str(h, "loc_name", l.name) == ESP_OK &&
            nvs_set_blob(h, "loc_lat", &l.lat, sizeof(float)) == ESP_OK &&
            nvs_set_blob(h, "loc_lon", &l.lon, sizeof(float)) == ESP_OK && nvs_commit(h) == ESP_OK;
  nvs_close(h);
  return ok;
}

bool settings_reset_location() {
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READWRITE, &h) != ESP_OK) return false;
  nvs_erase_key(h, "loc_name");
  nvs_erase_key(h, "loc_lat");
  nvs_erase_key(h, "loc_lon");
  bool ok = nvs_commit(h) == ESP_OK;
  nvs_close(h);
  return ok;
}

DisplaySettings settings_load_display() {
  DisplaySettings d = {120, true, kSaverBlack};
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READONLY, &h) != ESP_OK) return d;
  uint16_t t = 0;
  uint8_t hint = 0;
  if (nvs_get_u16(h, "scr_to", &t) == ESP_OK) d.timeout_s = t;
  if (nvs_get_u8(h, "scr_hint", &hint) == ESP_OK) d.hint = hint != 0;
  uint8_t saver = 0;
  if (nvs_get_u8(h, "scr_saver", &saver) == ESP_OK && saver <= kSaverAnalog) d.saver = saver;
  nvs_close(h);
  return d;
}

bool settings_save_display(const DisplaySettings& d) {
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READWRITE, &h) != ESP_OK) return false;
  bool ok = nvs_set_u16(h, "scr_to", d.timeout_s) == ESP_OK && nvs_set_u8(h, "scr_hint", d.hint ? 1 : 0) == ESP_OK &&
            nvs_set_u8(h, "scr_saver", d.saver) == ESP_OK &&
            nvs_commit(h) == ESP_OK;
  nvs_close(h);
  return ok;
}

TimeSettings settings_default_time() {
  TimeSettings t = {};
  snprintf(t.server, sizeof(t.server), "ch.pool.ntp.org");
  snprintf(t.tz_name, sizeof(t.tz_name), "Europe/Zurich");
  snprintf(t.tz, sizeof(t.tz), "CET-1CEST,M3.5.0,M10.5.0/3");
  return t;
}

TimeSettings settings_load_time() {
  TimeSettings t = settings_default_time();
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READONLY, &h) != ESP_OK) return t;
  TimeSettings n = {};
  bool ok = get_str(h, "ntp_srv", n.server, sizeof(n.server)) && n.server[0] != '\0' &&
            get_str(h, "tz_name", n.tz_name, sizeof(n.tz_name)) && get_str(h, "tz_posix", n.tz, sizeof(n.tz)) &&
            n.tz[0] != '\0';
  nvs_close(h);
  return ok ? n : t;
}

bool settings_save_time(const TimeSettings& t) {
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READWRITE, &h) != ESP_OK) return false;
  bool ok = nvs_set_str(h, "ntp_srv", t.server) == ESP_OK && nvs_set_str(h, "tz_name", t.tz_name) == ESP_OK &&
            nvs_set_str(h, "tz_posix", t.tz) == ESP_OK && nvs_commit(h) == ESP_OK;
  nvs_close(h);
  return ok;
}

namespace {
const Webcam kDefaultCams[] = {
    {1292514749, "Grabs \xC2\xB7 Nordwesten: Altmann"},
    {1670606347, "Buchs"},
    {1697029384, "Triesenberg: Gemeinde"},
    {1499936290, "Wartau \xC2\xB7 Westen: Seilbahn Palfries - Churfirsten"},
    {1697035782, "Wildhaus-Alt St. Johann: Wildhaus: Berggasthaus"},
};
}  // namespace

// NVS holds only the IDs: blob "cam_ids" (uint32_t[n], n = 0 stored as "cams_n").
int settings_load_cams(Webcam* out, int cap) {
  memset(out, 0, sizeof(Webcam) * cap);
  const int nd = static_cast<int>(sizeof(kDefaultCams) / sizeof(kDefaultCams[0]));
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READONLY, &h) == ESP_OK) {
    uint32_t ids[kMaxCams];
    size_t sz = sizeof(ids);
    uint8_t n8 = 0;
    bool ok = nvs_get_u8(h, "cams_n", &n8) == ESP_OK && n8 <= kMaxCams;
    if (ok && n8 > 0) ok = nvs_get_blob(h, "cam_ids", ids, &sz) == ESP_OK && sz == n8 * sizeof(uint32_t);
    nvs_close(h);
    if (ok) {
      int n = n8 > cap ? cap : n8;
      for (int i = 0; i < n; ++i) {
        out[i].id = ids[i];
        for (int k = 0; k < nd; ++k) {
          if (kDefaultCams[k].id == ids[i]) snprintf(out[i].title, sizeof(out[i].title), "%s", kDefaultCams[k].title);
        }
      }
      return n;
    }
  }
  int n = nd > cap ? cap : nd;
  memcpy(out, kDefaultCams, sizeof(Webcam) * n);
  return n;
}

bool settings_save_cams(const Webcam* cams, int n) {
  if (n < 0 || n > kMaxCams) return false;
  uint32_t ids[kMaxCams];
  for (int i = 0; i < n; ++i) ids[i] = cams[i].id;
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READWRITE, &h) != ESP_OK) return false;
  bool ok = true;
  if (n > 0) ok = nvs_set_blob(h, "cam_ids", ids, n * sizeof(uint32_t)) == ESP_OK;
  else nvs_erase_key(h, "cam_ids");
  ok = ok && nvs_set_u8(h, "cams_n", static_cast<uint8_t>(n)) == ESP_OK && nvs_commit(h) == ESP_OK;
  nvs_close(h);
  return ok;
}

bool settings_load_windy_key(char* out, size_t n, bool* own) {
  if (own != nullptr) *own = false;
  out[0] = '\0';
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READONLY, &h) == ESP_OK) {
    bool ok = get_str(h, "windy_key", out, n) && out[0] != '\0';
    nvs_close(h);
    if (ok) {
      if (own != nullptr) *own = true;
      return true;
    }
  }
  snprintf(out, n, "%s", CONFIG_PANEL_WINDY_API_KEY);
  return out[0] != '\0';
}

bool settings_save_windy_key(const char* key) {
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READWRITE, &h) != ESP_OK) return false;
  if (key == nullptr || key[0] == '\0') {
    nvs_erase_key(h, "windy_key");
  } else if (nvs_set_str(h, "windy_key", key) != ESP_OK) {
    nvs_close(h);
    return false;
  }
  bool ok = nvs_commit(h) == ESP_OK;
  nvs_close(h);
  return ok;
}

void settings_mask_key(const char* key, char* out, size_t n) {
  size_t len = strlen(key);
  if (len == 0) {
    snprintf(out, n, "-");
  } else if (len <= 8) {
    snprintf(out, n, "********");
  } else {
    snprintf(out, n, "%.4s****%s", key, key + len - 4);
  }
}
