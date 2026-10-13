#include "info.h"

#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_heap_caps.h"
#include "net.h"
#include "sys.h"
#include <esp_log.h>

static const char* const TAG = "info_net";

namespace {

uint8_t* psram(size_t n) { return static_cast<uint8_t*>(heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)); }

int64_t local_mktime(struct tm* t) { return static_cast<int64_t>(mktime(t)); }

// UTF-8 -> what the Inter fonts can draw (ASCII + Latin-1); a few typographic
// signs mapped, everything else (emoji, ...) dropped.
void sanitize(char* s) {
  unsigned char* r = reinterpret_cast<unsigned char*>(s);
  unsigned char* w = r;
  while (*r != 0) {
    if (*r < 0x80) {
      *w++ = *r++;
    } else if ((*r == 0xC2 || *r == 0xC3) && r[1] >= 0x80 && r[1] < 0xC0) {
      *w++ = *r++;
      *w++ = *r++;
    } else if (r[0] == 0xE2 && r[1] == 0x80 && r[2] != 0) {
      if (r[2] == 0x93 || r[2] == 0x94) *w++ = '-';
      else if (r[2] == 0x98 || r[2] == 0x99) *w++ = '\'';
      else if (r[2] == 0x9C || r[2] == 0x9D) *w++ = '"';
      r += 3;
    } else {
      int len = (*r & 0xE0) == 0xC0 ? 2 : (*r & 0xF0) == 0xE0 ? 3 : (*r & 0xF8) == 0xF0 ? 4 : 1;
      for (int i = 0; i < len && *r != 0; ++i) ++r;
    }
  }
  *w = 0;
  // collapse a leading space left by a dropped emoji
  if (s[0] == ' ') memmove(s, s + 1, strlen(s));
}

void url_encode(const char* in, char* out, size_t n) {
  static const char* hex = "0123456789ABCDEF";
  size_t o = 0;
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(in); *p != 0 && o + 4 < n; ++p) {
    if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '-' ||
        *p == '.' || *p == '_') {
      out[o++] = static_cast<char>(*p);
    } else {
      out[o++] = '%';
      out[o++] = hex[*p >> 4];
      out[o++] = hex[*p & 15];
    }
  }
  out[o] = '\0';
}

}  // namespace

const char* warning_name(uint8_t type) {
  static const char* k[12] = {"Wind",        "Gewitter",  "Regen",   "Schnee",  "Gl\xC3\xA4tte",  "Frost",
                              "Rutschungen", "Hitze",     "Lawinen", "Erdbeben", "Waldbrand", "Hochwasser"};
  return type < 12 ? k[type] : "Warnung";
}

int transit_search(const char* query, Station* out, int cap) {
  char q[150];
  url_encode(query, q, sizeof(q));
  char url[300];
  snprintf(url, sizeof(url),
           "https://transport.opendata.ch/v1/locations?type=station&query=%s"
           "&fields[]=stations/id&fields[]=stations/name",
           q);
  constexpr size_t kCap = 16 * 1024;
  uint8_t* body = psram(kCap);
  if (body == nullptr) return -1;
  HttpGetResult http = http_get(url, nullptr, nullptr, body, kCap, 10000);
  if (!http.ok || http.status != 200) {
    ESP_LOGW(TAG, "Transit search HTTP %d %s", http.status, http.error);
    heap_caps_free(body);
    return -1;
  }
  DynamicJsonDocument doc(8192);
  DeserializationError err = deserializeJson(doc, reinterpret_cast<const char*>(body), http.length);
  heap_caps_free(body);
  if (err) return -1;
  int n = 0;
  for (JsonObject s : doc["stations"].as<JsonArray>()) {
    const char* id = s["id"] | "";
    if (id[0] == '\0' || n >= cap) continue;  // addresses/POIs have no id
    snprintf(out[n].id, sizeof(out[n].id), "%s", id);
    snprintf(out[n].name, sizeof(out[n].name), "%s", s["name"] | "?");
    sanitize(out[n].name);
    n++;
  }
  console_printf("Transit search \"%s\" -> %d stations\n", query, n);
  return n;
}

bool transit_board(const Station& st, Board* out) {
  memset(out, 0, sizeof(*out));
  out->st = st;
  char url[400];
  snprintf(url, sizeof(url),
           "https://transport.opendata.ch/v1/stationboard?id=%s&limit=%d"
           "&fields[]=stationboard/category&fields[]=stationboard/number&fields[]=stationboard/to"
           "&fields[]=stationboard/stop/departureTimestamp&fields[]=stationboard/stop/delay"
           "&fields[]=stationboard/stop/platform&fields[]=stationboard/stop/prognosis/platform",
           st.id, kMaxDeps);
  constexpr size_t kCap = 24 * 1024;
  uint8_t* body = psram(kCap);
  if (body == nullptr) return false;
  HttpGetResult http = http_get(url, nullptr, nullptr, body, kCap, 10000);
  if (!http.ok || http.status != 200) {
    ESP_LOGW(TAG, "Stationboard %s HTTP %d %s", st.id, http.status, http.error);
    heap_caps_free(body);
    return false;
  }
  DynamicJsonDocument doc(12288);
  DeserializationError err = deserializeJson(doc, reinterpret_cast<const char*>(body), http.length);
  heap_caps_free(body);
  if (err) {
    ESP_LOGW(TAG, "Stationboard JSON %s", err.c_str());
    return false;
  }
  for (JsonObject j : doc["stationboard"].as<JsonArray>()) {
    if (out->n >= kMaxDeps) break;
    Departure& d = out->d[out->n];
    const char* cat = j["category"] | "";
    const char* num = j["number"] | "";
    d.bus = strcmp(cat, "B") == 0 || strcmp(cat, "BUS") == 0 || strcmp(cat, "NFB") == 0 || strcmp(cat, "T") == 0;
    // "S" + "4" -> "S4"; buses show just the number with a bus icon.
    if (d.bus) snprintf(d.line, sizeof(d.line), "%s", num[0] != '\0' ? num : cat);
    else if (strlen(cat) <= 2) snprintf(d.line, sizeof(d.line), "%s%s", cat, num);
    else snprintf(d.line, sizeof(d.line), "%s %s", cat, num);
    snprintf(d.to, sizeof(d.to), "%s", j["to"] | "");
    sanitize(d.to);
    JsonObject stop = j["stop"];
    d.dep = stop["departureTimestamp"] | 0LL;
    d.delay = stop["delay"] | 0;
    const char* pl = stop["platform"] | "";
    const char* ppl = stop["prognosis"]["platform"] | "";
    if (ppl[0] != '\0' && strcmp(ppl, pl) != 0) snprintf(d.platform, sizeof(d.platform), "%s!", ppl);
    else snprintf(d.platform, sizeof(d.platform), "%s", pl);
    if (d.dep > 0) out->n++;
  }
  out->fetched = time(nullptr);
  out->ok = true;
  console_printf("Stationboard %s: %d departures (%u bytes)\n", st.name, out->n, static_cast<unsigned>(http.length));
  return true;
}

bool calendar_fetch(const char* ical_url, const char* waste_url, Calendar* out) {
  memset(out, 0, sizeof(*out));
  const int64_t now = time(nullptr);
  // Today from local midnight, so all-day entries of today still count.
  time_t tn = static_cast<time_t>(now);
  struct tm lt;
  localtime_r(&tn, &lt);
  lt.tm_hour = 0;
  lt.tm_min = 0;
  lt.tm_sec = 0;
  lt.tm_isdst = -1;
  const int64_t midnight = mktime(&lt);
  out->have_events_url = ical_url != nullptr && ical_url[0] != '\0';
  if (out->have_events_url) {
    constexpr size_t kCap = 512 * 1024;
    uint8_t* body = psram(kCap + 1);
    if (body != nullptr) {
      HttpGetResult http = http_get(ical_url, nullptr, nullptr, body, kCap, 20000);
      if (http.ok && http.status == 200 && http.length > 15 && memcmp(body, "BEGIN:VCALENDAR", 15) == 0) {
        out->n_events = ical_parse(reinterpret_cast<char*>(body), http.length, now, now + 60LL * 86400, out->events,
                                   kMaxEvents, 0, local_mktime);
        for (int i = 0; i < out->n_events; ++i) sanitize(out->events[i].title);
        out->ok_events = true;
        console_printf("Calendar: %d events in 60 days (%u bytes)\n", out->n_events, static_cast<unsigned>(http.length));
      } else {
        ESP_LOGW(TAG, "Calendar HTTP %d %s", http.status, http.error);  // never log the private URL
      }
      heap_caps_free(body);
    }
  }
  if (waste_url != nullptr && waste_url[0] != '\0') {
    constexpr size_t kCap = 1024 * 1024;  // the Buchs SG page is ~650 KB of HTML
    uint8_t* body = psram(kCap + 1);
    if (body != nullptr) {
      HttpGetResult http = http_get(waste_url, nullptr, nullptr, body, kCap, 30000);
      if (http.ok && http.status == 200) {
        body[http.length] = 0;
        if (http.length > 15 && memcmp(body, "BEGIN:VCALENDAR", 15) == 0) {
          out->n_waste = ical_parse(reinterpret_cast<char*>(body), http.length, midnight, now + 45LL * 86400,
                                    out->waste, kMaxWaste, 1, local_mktime);
        } else {
          out->n_waste = waste_parse_html(reinterpret_cast<char*>(body), http.length, midnight, now + 45LL * 86400,
                                          out->waste, kMaxWaste, local_mktime);
        }
        for (int i = 0; i < out->n_waste; ++i) sanitize(out->waste[i].title);
        out->ok_waste = out->n_waste > 0;
        console_printf("Waste: %d dates (%u bytes)\n", out->n_waste, static_cast<unsigned>(http.length));
      } else {
        ESP_LOGW(TAG, "Waste HTTP %d %s", http.status, http.error);
      }
      heap_caps_free(body);
    }
  }
  out->fetched = now;
  return out->ok_events || out->ok_waste;
}

bool meteo_fetch(const char* plz, MeteoNow* out) {
  memset(out, 0, sizeof(*out));
  char url[160];
  snprintf(url, sizeof(url), "https://app-prod-ws.meteoswiss-app.ch/v1/plzDetail?plz=%s", plz);
  constexpr size_t kCap = 64 * 1024;
  uint8_t* body = psram(kCap);
  if (body == nullptr) return false;
  HttpGetResult http = http_get(url, nullptr, nullptr, body, kCap, 15000, "Accept-Language", "de");
  if (!http.ok || http.status != 200) {
    ESP_LOGW(TAG, "MeteoSchweiz HTTP %d %s", http.status, http.error);
    heap_caps_free(body);
    return false;
  }
  StaticJsonDocument<512> filter;
  for (const char* k : {"warnType", "warnLevel", "text", "validFrom", "validTo", "outlook"}) filter["warnings"][0][k] = true;
  filter["graph"]["start"] = true;
  filter["graph"]["precipitation10m"] = true;
  filter["graph"]["precipitationMin10m"] = true;
  filter["graph"]["precipitationMax10m"] = true;
  DynamicJsonDocument doc(24576);
  DeserializationError err = deserializeJson(doc, reinterpret_cast<const char*>(body), http.length,
                                             DeserializationOption::Filter(filter));
  heap_caps_free(body);
  if (err) {
    ESP_LOGW(TAG, "MeteoSchweiz JSON %s", err.c_str());
    return false;
  }
  const int64_t now = time(nullptr);
  for (JsonObject w : doc["warnings"].as<JsonArray>()) {
    int level = w["warnLevel"] | 0;
    int64_t to = (w["validTo"] | 0LL) / 1000;
    if (level < 2 || (to != 0 && to < now)) continue;
    Warning x = {};
    x.type = static_cast<uint8_t>(w["warnType"] | 99);
    x.level = static_cast<uint8_t>(level);
    x.outlook = w["outlook"] | false;
    x.from = (w["validFrom"] | 0LL) / 1000;
    x.to = to;
    snprintf(x.text, sizeof(x.text), "%s", w["text"] | "");
    sanitize(x.text);
    // insert by level (highest first)
    int pos = out->n;
    while (pos > 0 && out->w[pos - 1].level < x.level) --pos;
    if (pos >= kMaxWarnings) continue;
    int last = out->n < kMaxWarnings ? out->n : kMaxWarnings - 1;
    for (int i = last; i > pos; --i) out->w[i] = out->w[i - 1];
    out->w[pos] = x;
    if (out->n < kMaxWarnings) out->n++;
  }
  JsonObject g = doc["graph"];
  int64_t start = (g["start"] | 0LL) / 1000;
  JsonArray p = g["precipitation10m"];
  int first = start > 0 && now > start ? static_cast<int>((now - start) / 600) : 0;
  out->rain_start = start + static_cast<int64_t>(first) * 600;
  for (int i = 0; i < kRainSteps; ++i) {
    out->rain[i] = p[first + i] | 0.0f;
    out->rain_min[i] = g["precipitationMin10m"][first + i] | 0.0f;
    out->rain_max[i] = g["precipitationMax10m"][first + i] | 0.0f;
  }
  out->fetched = now;
  out->ok = start > 0 && static_cast<int>(p.size()) > first;
  console_printf("MeteoSchweiz %s: %d warnings, rain[0..5] %.1f %.1f %.1f %.1f %.1f %.1f mm/h\n", plz, out->n,
                 out->rain[0], out->rain[1], out->rain[2], out->rain[3], out->rain[4], out->rain[5]);
  return true;
}
