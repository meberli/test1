#include "settings.h"

#include <stdio.h>
#include <string.h>

#include "config_load.h"
#include "info.h"
#include <ArduinoJson.h>
#include "esp_heap_caps.h"
#include "nvs.h"
#include "sdkconfig.h"
#include "settings_schema.h"
#include "sys.h"

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
    uint8_t seeded = 0;
    nvs_get_u8(h, "wifi_seeded", &seeded);
    nvs_close(h);
    if (ok) return true;
    // Once NVS owns the Wi-Fi settings (seeded or set by the user), an empty
    // SSID means "not configured": the build default never comes back.
    if (seeded) return false;
  }
  snprintf(out->ssid, sizeof(out->ssid), "%s", WIFI_SSID);
  snprintf(out->pass, sizeof(out->pass), "%s", WIFI_PASSWORD);
  return out->ssid[0] != '\0';
}

bool settings_save_wifi(const WifiCreds& c) {
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READWRITE, &h) != ESP_OK) return false;
  bool ok = nvs_set_str(h, "wifi_ssid", c.ssid) == ESP_OK && nvs_set_str(h, "wifi_pass", c.pass) == ESP_OK &&
            nvs_set_u8(h, "wifi_seeded", 1) == ESP_OK && nvs_commit(h) == ESP_OK;
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
  if (out[0] == '\0' && nvs_open(kNs, NVS_READONLY, &h) == ESP_OK) {
    if (!get_str(h, "windy_dflt", out, n)) out[0] = '\0';  // seeded from an earlier build
    nvs_close(h);
  }
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

void settings_load_stations(Station* out) {
  static const Station kDefault[kStationN] = {{"8509404", "Buchs SG"}, {"8587407", "R\xC3\xA4" "fis, Schulhaus"}};
  nvs_handle_t h;
  bool open = nvs_open(kNs, NVS_READONLY, &h) == ESP_OK;
  for (int i = 0; i < kStationN; ++i) {
    out[i] = kDefault[i];
    if (!open) continue;
    char kid[8], kname[10];
    snprintf(kid, sizeof(kid), "st%d_id", i);
    snprintf(kname, sizeof(kname), "st%d_name", i);
    Station s = {};
    if (get_str(h, kid, s.id, sizeof(s.id)) && s.id[0] != '\0' && get_str(h, kname, s.name, sizeof(s.name))) out[i] = s;
  }
  if (open) nvs_close(h);
}

bool settings_save_station(int i, const Station& st) {
  if (i < 0 || i >= kStationN) return false;
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READWRITE, &h) != ESP_OK) return false;
  char kid[8], kname[10];
  snprintf(kid, sizeof(kid), "st%d_id", i);
  snprintf(kname, sizeof(kname), "st%d_name", i);
  bool ok = nvs_set_str(h, kid, st.id) == ESP_OK && nvs_set_str(h, kname, st.name) == ESP_OK && nvs_commit(h) == ESP_OK;
  nvs_close(h);
  return ok;
}

namespace {
const char* const kTextKey[kTextSettingN] = {"ical_url", "waste_url", "ota_url", "meteo_plz"};
}  // namespace

const char* settings_text_default(TextSetting which) {
  switch (which) {
    case kUrlIcal: return CONFIG_PANEL_ICAL_URL;
    case kUrlWaste: return CONFIG_PANEL_WASTE_URL;
    case kUrlOta: return CONFIG_PANEL_OTA_URL;
    case kTextPlz: return CONFIG_PANEL_METEO_PLZ;
    default: return "";
  }
}

bool settings_load_text(TextSetting which, char* out, size_t n, bool* own) {
  if (own != nullptr) *own = false;
  out[0] = '\0';
  if (which >= kTextSettingN) return false;
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READONLY, &h) == ESP_OK) {
    bool ok = get_str(h, kTextKey[which], out, n) && out[0] != '\0';
    nvs_close(h);
    if (ok) {
      if (own != nullptr) *own = true;
      return true;
    }
  }
  snprintf(out, n, "%s", settings_text_default(which));
  if (out[0] == '\0' && which == kUrlIcal && nvs_open(kNs, NVS_READONLY, &h) == ESP_OK) {
    if (!get_str(h, "ical_dflt", out, n)) out[0] = '\0';  // seeded from an earlier build
    nvs_close(h);
  }
  return out[0] != '\0';
}

bool settings_save_text(TextSetting which, const char* value) {
  if (which >= kTextSettingN) return false;
  if (value != nullptr && value[0] != '\0') {
    const KeySpec* ks = schema_find(kTextKey[which]);
    const char* why = "";
    if (ks != nullptr && !schema_check_str(*ks, value, &why)) {
      console_printf("Setting %s rejected: %s\n", kTextKey[which], why);
      return false;
    }
  }
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READWRITE, &h) != ESP_OK) return false;
  bool ok = true;
  if (value == nullptr || value[0] == '\0') nvs_erase_key(h, kTextKey[which]);
  else ok = nvs_set_str(h, kTextKey[which], value) == ESP_OK;
  ok = ok && nvs_commit(h) == ESP_OK;
  nvs_close(h);
  return ok;
}

// {"format":"esp_display-settings","version":1,"cfg_ver":1,"secrets":false,
//  "keys":{"loc_name":{"t":"str","v":"Buchs SG"},"scr_to":{"t":"u16","v":120},
//          "loc_lat":{"t":"blob","v":"<hex>"}}}
// Only keys of the schema (settings_schema.cpp) are exported; secrets only on request.
size_t settings_export_json(char* out, size_t n, bool secrets) {
  DynamicJsonDocument doc(16384);
  doc["format"] = "esp_display-settings";
  doc["version"] = 1;
  doc["cfg_ver"] = kCfgVersion;
  doc["secrets"] = secrets;
  JsonObject keys = doc.createNestedObject("keys");
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READONLY, &h) != ESP_OK) {
    return serializeJson(doc, out, n);
  }
  const KeySpec* spec = schema_begin();
  for (size_t i = 0; i < schema_count(); ++i) {
    const KeySpec& ks = spec[i];
    if (ks.flags & kKeyInternal) continue;
    if (!secrets && (ks.flags & kKeySecret)) continue;
    switch (ks.type) {
      case KeyType::U8: {
        uint8_t v;
        if (nvs_get_u8(h, ks.key, &v) != ESP_OK) break;
        JsonObject k = keys.createNestedObject(ks.key);
        k["t"] = "u8";
        k["v"] = v;
        break;
      }
      case KeyType::U16: {
        uint16_t v;
        if (nvs_get_u16(h, ks.key, &v) != ESP_OK) break;
        JsonObject k = keys.createNestedObject(ks.key);
        k["t"] = "u16";
        k["v"] = v;
        break;
      }
      case KeyType::Str: {
        char v[300];
        size_t len = sizeof(v);
        if (nvs_get_str(h, ks.key, v, &len) != ESP_OK) break;
        JsonObject k = keys.createNestedObject(ks.key);
        k["t"] = "str";
        k["v"] = v;  // copied by ArduinoJson
        memset(v, 0, sizeof(v));
        break;
      }
      case KeyType::Blob: {
        uint8_t v[256];
        size_t len = sizeof(v);
        if (nvs_get_blob(h, ks.key, v, &len) != ESP_OK) break;
        char hex[sizeof(v) * 2 + 1];
        for (size_t b = 0; b < len; ++b) snprintf(hex + b * 2, 3, "%02x", v[b]);
        hex[len * 2] = '\0';
        JsonObject k = keys.createNestedObject(ks.key);
        k["t"] = "blob";
        k["v"] = hex;
        break;
      }
    }
  }
  nvs_close(h);
  if (measureJson(doc) + 1 > n) return 0;
  return serializeJson(doc, out, n);
}

namespace {

bool parse_hex(const char* hex, uint8_t* out, size_t cap, size_t* n) {
  size_t len = strlen(hex);
  if (len % 2 != 0 || len / 2 > cap) return false;
  for (size_t i = 0; i < len / 2; ++i) {
    unsigned v = 0;
    for (int k = 0; k < 2; ++k) {
      char c = hex[i * 2 + k];
      int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
      if (d < 0) return false;
      v = v * 16 + static_cast<unsigned>(d);
    }
    out[i] = static_cast<uint8_t>(v);
  }
  *n = len / 2;
  return true;
}

const char* type_name(KeyType t) {
  switch (t) {
    case KeyType::U8: return "u8";
    case KeyType::U16: return "u16";
    case KeyType::Str: return "str";
    default: return "blob";
  }
}

}  // namespace

// Two passes: validate every key against the schema, then write. Any invalid
// known key rejects the whole file (nothing written); unknown keys (e.g. from a
// newer firmware) are skipped and listed.
int settings_import_json(const char* json, size_t len, char* report, size_t rn) {
  if (report != nullptr && rn > 0) report[0] = '\0';
  auto note = [&](const char* fmt, const char* a, const char* b) {
    if (report == nullptr || rn == 0) return;
    size_t used = strlen(report);
    if (used + 1 < rn) snprintf(report + used, rn - used, fmt, a, b);
  };
  DynamicJsonDocument doc(16384);
  if (deserializeJson(doc, json, len)) return -1;
  if (strcmp(doc["format"] | "", "esp_display-settings") != 0) return -1;
  int ver = doc["cfg_ver"] | 1;
  if (ver > kCfgVersion) {
    return -2;
  }
  JsonObject keys = doc["keys"].as<JsonObject>();
  if (keys.isNull()) return -1;
  int bad = 0, skipped = 0;
  for (int pass = 0; pass < 2; ++pass) {
    nvs_handle_t h = 0;
    if (pass == 1) {
      if (bad > 0) return -3;
      if (nvs_open(kNs, NVS_READWRITE, &h) != ESP_OK) return -1;
    }
    int written = 0;
    for (JsonPair kv : keys) {
      const char* key = kv.key().c_str();
      const KeySpec* ks = schema_find(key);
      if (ks == nullptr || (ks->flags & kKeyInternal)) {
        if (pass == 0) {
          ++skipped;
          note("unbekannt: %s%s; ", key, "");
        }
        continue;
      }
      const char* t = kv.value()["t"] | "";
      JsonVariant v = kv.value()["v"];
      const char* why = "falscher Typ";
      bool ok = strcmp(t, type_name(ks->type)) == 0;
      uint8_t blob[256];
      size_t bl = 0;
      if (ok) {
        switch (ks->type) {
          case KeyType::U8:
          case KeyType::U16:
            ok = v.is<long long>() && schema_check_int(*ks, v.as<long long>(), &why);
            break;
          case KeyType::Str:
            ok = v.is<const char*>() && schema_check_str(*ks, v.as<const char*>(), &why);
            break;
          case KeyType::Blob:
            ok = v.is<const char*>() && parse_hex(v.as<const char*>(), blob, sizeof(blob), &bl);
            if (!ok) why = "kein Hex";
            ok = ok && schema_check_blob(*ks, blob, bl, &why);
            break;
        }
      }
      if (pass == 0) {
        if (!ok) {
          ++bad;
          note("%s: %s; ", key, why);
        }
        continue;
      }
      esp_err_t e = ESP_FAIL;
      switch (ks->type) {
        case KeyType::U8: e = nvs_set_u8(h, key, v.as<uint8_t>()); break;
        case KeyType::U16: e = nvs_set_u16(h, key, v.as<uint16_t>()); break;
        case KeyType::Str: e = nvs_set_str(h, key, v.as<const char*>()); break;
        case KeyType::Blob: e = nvs_set_blob(h, key, blob, bl); break;
      }
      if (e == ESP_OK) ++written;
    }
    if (pass == 1) {
      nvs_set_u8(h, "cfg_ver", kCfgVersion);
      nvs_commit(h);
      nvs_close(h);
      (void)skipped;
      return written;
    }
  }
  return -1;
}

void settings_migrate() {
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READWRITE, &h) != ESP_OK) return;
  uint8_t v = 0;
  if (nvs_get_u8(h, "cfg_ver", &v) != ESP_OK) v = 0;
  if (v < kCfgVersion) {
    // 0 -> 1: the layout of v0.7.5 is version 1; nothing to convert yet.
    // Future steps: if (v < 2) { ... rename/convert keys ... }
    nvs_set_u8(h, "cfg_ver", kCfgVersion);
    nvs_commit(h);
    console_printf("Settings: config version %u -> %u\n", v, kCfgVersion);
  }
  nvs_close(h);
}

// --- Termin-Erinnerungen ---------------------------------------------------------

ReminderSettings settings_load_reminders() {
  ReminderSettings r = {true, true, true, 1, 4};
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READONLY, &h) != ESP_OK) return r;
  uint8_t v = 0;
  if (nvs_get_u8(h, "rem_on", &v) == ESP_OK) r.on = v != 0;
  if (nvs_get_u8(h, "rem_src", &v) == ESP_OK) {
    r.cal = (v & 1) != 0;
    r.waste = (v & 2) != 0;
  }
  if (nvs_get_u8(h, "rem_lcal", &v) == ESP_OK && v < kLeadN) r.lead_cal = v;
  if (nvs_get_u8(h, "rem_lwst", &v) == ESP_OK && v < kLeadN) r.lead_waste = v;
  nvs_close(h);
  return r;
}

uint16_t settings_load_waste_until() {
  uint16_t v = kWasteUntilDefault;
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READONLY, &h) != ESP_OK) return v;
  uint16_t t = 0;
  if (nvs_get_u16(h, "wst_until", &t) == ESP_OK && t <= 1440) v = t;
  nvs_close(h);
  return v;
}

bool settings_save_waste_until(uint16_t minutes) {
  if (minutes > 1440) return false;
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READWRITE, &h) != ESP_OK) return false;
  bool ok = nvs_set_u16(h, "wst_until", minutes) == ESP_OK && nvs_commit(h) == ESP_OK;
  nvs_close(h);
  return ok;
}

bool settings_save_reminders(const ReminderSettings& r) {
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READWRITE, &h) != ESP_OK) return false;
  uint8_t src = (r.cal ? 1 : 0) | (r.waste ? 2 : 0);
  bool ok = nvs_set_u8(h, "rem_on", r.on ? 1 : 0) == ESP_OK && nvs_set_u8(h, "rem_src", src) == ESP_OK &&
            nvs_set_u8(h, "rem_lcal", r.lead_cal) == ESP_OK && nvs_set_u8(h, "rem_lwst", r.lead_waste) == ESP_OK &&
            nvs_commit(h) == ESP_OK;
  nvs_close(h);
  return ok;
}

namespace {
constexpr int kAckN = 48;
struct AckRing {
  uint32_t key[kAckN];
  uint8_t next;
};
AckRing ack_ring;
bool ack_loaded = false;

void ack_load() {
  if (ack_loaded) return;
  ack_loaded = true;
  memset(&ack_ring, 0, sizeof(ack_ring));
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READONLY, &h) != ESP_OK) return;
  size_t sz = sizeof(ack_ring);
  if (nvs_get_blob(h, "rem_ack", &ack_ring, &sz) != ESP_OK || sz != sizeof(ack_ring)) memset(&ack_ring, 0, sizeof(ack_ring));
  nvs_close(h);
  if (ack_ring.next >= kAckN) ack_ring.next = 0;
}
}  // namespace

bool settings_reminder_acked(uint32_t key) {
  ack_load();
  for (uint32_t k : ack_ring.key) {
    if (k == key && key != 0) return true;
  }
  return false;
}

bool settings_reminder_ack(uint32_t key) {
  if (settings_reminder_acked(key)) return true;
  ack_ring.key[ack_ring.next] = key;
  ack_ring.next = static_cast<uint8_t>((ack_ring.next + 1) % kAckN);
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READWRITE, &h) != ESP_OK) return false;
  bool ok = nvs_set_blob(h, "rem_ack", &ack_ring, sizeof(ack_ring)) == ESP_OK && nvs_commit(h) == ESP_OK;
  nvs_close(h);
  return ok;
}

void settings_reminder_clear_acks() {
  memset(&ack_ring, 0, sizeof(ack_ring));
  ack_loaded = true;
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READWRITE, &h) != ESP_OK) return;
  nvs_erase_key(h, "rem_ack");
  nvs_commit(h);
  nvs_close(h);
}

// --- Seeding build-time secrets into NVS ---------------------------------------------

namespace {
// Writes value under key when the key is missing (or, with update, differs).
void seed(nvs_handle_t h, const char* key, const char* value, bool update, int* written) {
  if (value == nullptr || value[0] == '\0') return;
  char cur[256];
  bool have = get_str(h, key, cur, sizeof(cur));
  if (have && (!update || strcmp(cur, value) == 0)) return;
  if (nvs_set_str(h, key, value) == ESP_OK) ++*written;
}
}  // namespace

void settings_seed_from_build() {
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READWRITE, &h) != ESP_OK) return;
  int n = 0;
  // Wi-Fi and Loxone: only when NVS has no value yet (the user may have changed them).
  // The build Wi-Fi is copied once ("wifi_seeded"); afterwards a network the
  // user picked (or cleared) is never overridden by the firmware default.
  char ssid[33];
  uint8_t seeded = 0;
  nvs_get_u8(h, "wifi_seeded", &seeded);
  if (!seeded) {
    if (!get_str(h, "wifi_ssid", ssid, sizeof(ssid)) || ssid[0] == '\0') {
      seed(h, "wifi_ssid", WIFI_SSID, true, &n);
      seed(h, "wifi_pass", WIFI_PASSWORD, true, &n);
    }
    if (get_str(h, "wifi_ssid", ssid, sizeof(ssid)) && ssid[0] != '\0') {
      nvs_set_u8(h, "wifi_seeded", 1);
      ++n;
    }
  }
  char host[64];
  if (!get_str(h, "lox_host", host, sizeof(host)) || host[0] == '\0') {
    seed(h, "lox_host", LOXONE_HOST, true, &n);
    seed(h, "lox_user", LOXONE_USER, true, &n);
    seed(h, "lox_pass", LOXONE_PASSWORD, true, &n);
    seed(h, "lox_auth", LOXONE_AUTH, true, &n);
    if (LOXONE_HOST[0] != '\0') {
      nvs_set_u16(h, "lox_port", static_cast<uint16_t>(LOXONE_PORT));
    }
  }
  // Build defaults for the Windy key and iCal URL: kept as a separate "default"
  // copy (the user's own value stays in windy_key / ical_url).
  seed(h, "windy_dflt", CONFIG_PANEL_WINDY_API_KEY, true, &n);
  seed(h, "ical_dflt", CONFIG_PANEL_ICAL_URL, true, &n);
  if (n > 0) nvs_commit(h);
  nvs_close(h);
  printf("Settings: %d build values copied to NVS\n", n);
}

#ifdef CONFIG_PANEL_TEST_HARNESS
// Test only: forget the Wi-Fi (as after a user reset), set a password, or
// restore the build credentials. Values are never logged.
bool settings_test_wifi(int op, const char* arg) {
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READWRITE, &h) != ESP_OK) return false;
  bool ok = true;
  if (op == 0) {
    nvs_erase_key(h, "wifi_ssid");
    nvs_erase_key(h, "wifi_pass");
    nvs_set_u8(h, "wifi_seeded", 1);
  } else if (op == 1) {
    ok = nvs_set_str(h, "wifi_pass", arg) == ESP_OK;
  } else {
    ok = nvs_set_str(h, "wifi_ssid", WIFI_SSID) == ESP_OK && nvs_set_str(h, "wifi_pass", WIFI_PASSWORD) == ESP_OK;
    nvs_set_u8(h, "wifi_seeded", 1);
  }
  ok = ok && nvs_commit(h) == ESP_OK;
  nvs_close(h);
  return ok;
}
#endif

LoxoneConfig settings_load_loxone() {
  LoxoneConfig c = {};
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READONLY, &h) == ESP_OK) {
    bool ok = get_str(h, "lox_host", c.host, sizeof(c.host)) && c.host[0] != '\0';
    if (ok) {
      get_str(h, "lox_user", c.user, sizeof(c.user));
      get_str(h, "lox_pass", c.pass, sizeof(c.pass));
      if (!get_str(h, "lox_auth", c.auth, sizeof(c.auth)) || c.auth[0] == '\0') snprintf(c.auth, sizeof(c.auth), "basic");
      c.port = 80;
      nvs_get_u16(h, "lox_port", &c.port);
    }
    nvs_close(h);
    if (ok) return c;
  }
  snprintf(c.host, sizeof(c.host), "%s", LOXONE_HOST);
  c.port = static_cast<uint16_t>(LOXONE_PORT);
  snprintf(c.user, sizeof(c.user), "%s", LOXONE_USER);
  snprintf(c.pass, sizeof(c.pass), "%s", LOXONE_PASSWORD);
  snprintf(c.auth, sizeof(c.auth), "%s", LOXONE_AUTH);
  return c;
}

const LoxoneConfig& settings_loxone() {
  static LoxoneConfig c = settings_load_loxone();
  return c;
}
