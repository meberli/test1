#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

#include <stdio.h>
#include <string.h>

#include "config_load.h"
#include "loxone_auth.h"
#include "loxone_client.h"
#include "loxone_structure.h"
#include "panel_hw.h"
#include "panel_ui.h"
#include "board_pins.h"
#include "test_harness.h"

#ifndef SERIAL_BAUD
#define SERIAL_BAUD 115200
#endif

#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
#error USB CDC must stay off: GPIO19/20 are touch and LCD pins, and USB-C is the CH340 UART
#endif
#if !defined(ARDUINO_USB_MODE) || ARDUINO_USB_MODE != 0
#error ARDUINO_USB_MODE must be 0 so GPIO19/GPIO20 stay available for touch and the LCD
#endif

namespace {

enum class Phase { NotConfigured, Wifi, Loading, Ready, Failed };

Phase phase = Phase::NotConfigured;
bool load_painted = false;
uint32_t wifi_started_ms = 0;
constexpr uint32_t kWifiTimeoutMs = 25000;
constexpr size_t kStructureCap = 768 * 1024;

LoxoneBasicAuthorizer basic_auth(LOXONE_USER, LOXONE_PASSWORD);
LoxoneTokenAuthorizer token_auth;
LoxoneAuthorizer* auth = nullptr;
uint32_t next_weather_ms = 0;

// Buchs, St. Gallen. Open-Meteo needs no key. TLS is not pinned.
// One request: current conditions, 7 daily summaries and hourly values (the
// hourly time array is filtered out; hour i of day d is index d*24+i).
// The parsed document (~15 KB) is allocated from the heap, which puts large
// blocks in PSRAM on this board.
static WeatherDetailDay week[kWxDays];

bool fetch_buchs_weather(int* temp_c, int* code, WeatherDay* days, int* week_count) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(10000);
  const char* url =
      "https://api.open-meteo.com/v1/forecast?latitude=47.3925&longitude=9.4728"
      "&current=temperature_2m,weather_code"
      "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_sum,"
      "sunrise,sunset,wind_speed_10m_max,precipitation_probability_max"
      "&hourly=temperature_2m,precipitation,precipitation_probability,weather_code,wind_speed_10m"
      "&timezone=Europe%2FZurich&forecast_days=7";
  if (!http.begin(client, url)) {
    return false;
  }
  int status = http.GET();
  if (status != 200) {
    Serial.printf("Weather HTTP %d\n", status);
    http.end();
    return false;
  }
  String payload = http.getString();
  http.end();
  StaticJsonDocument<768> filter;
  filter["current"]["temperature_2m"] = true;
  filter["current"]["weather_code"] = true;
  const char* dkeys[] = {"time", "weather_code", "temperature_2m_max", "temperature_2m_min", "precipitation_sum",
                         "sunrise", "sunset", "wind_speed_10m_max", "precipitation_probability_max"};
  for (const char* k : dkeys) filter["daily"][k] = true;
  const char* hkeys[] = {"temperature_2m", "precipitation", "precipitation_probability", "weather_code",
                         "wind_speed_10m"};
  for (const char* k : hkeys) filter["hourly"][k] = true;
  DynamicJsonDocument doc(24576);
  DeserializationError err = deserializeJson(doc, payload, DeserializationOption::Filter(filter));
  if (err) {
    Serial.printf("Weather JSON %s (%u bytes)\n", err.c_str(), static_cast<unsigned>(payload.length()));
    return false;
  }
  JsonVariant temp = doc["current"]["temperature_2m"];
  JsonVariant wcode = doc["current"]["weather_code"];
  if (temp.isNull() || wcode.isNull()) {
    return false;
  }
  float value = temp.as<float>();
  *temp_c = static_cast<int>(value >= 0.f ? value + 0.5f : value - 0.5f);
  *code = wcode.as<int>();
  JsonObject daily = doc["daily"];
  JsonObject hourly = doc["hourly"];
  int n = 0;
  for (int d = 0; d < kWxDays; ++d) {
    if (daily["weather_code"][d].isNull() || daily["temperature_2m_max"][d].isNull() ||
        daily["temperature_2m_min"][d].isNull()) {
      break;
    }
    WeatherDetailDay& w = week[d];
    memset(&w, 0, sizeof(w));
    snprintf(w.date, sizeof(w.date), "%s", daily["time"][d] | "");
    const char* sr = daily["sunrise"][d] | "";  // "2026-10-12T07:38"
    const char* ss = daily["sunset"][d] | "";
    snprintf(w.sunrise, sizeof(w.sunrise), "%s", strlen(sr) >= 16 ? sr + 11 : "--:--");
    snprintf(w.sunset, sizeof(w.sunset), "%s", strlen(ss) >= 16 ? ss + 11 : "--:--");
    w.code = daily["weather_code"][d].as<int>();
    w.t_max = daily["temperature_2m_max"][d].as<float>();
    w.t_min = daily["temperature_2m_min"][d].as<float>();
    w.precip = daily["precipitation_sum"][d] | 0.0f;
    w.prob_max = daily["precipitation_probability_max"][d] | 0;
    w.wind_max = daily["wind_speed_10m_max"][d] | 0.0f;
    for (int s = 0; s < kWxSlots; ++s) {
      int h0 = d * 24 + s * 3;
      WeatherSlot& sl = w.slot[s];
      sl.temp = hourly["temperature_2m"][h0] | 0.0f;
      sl.code = hourly["weather_code"][h0 + 1] | (hourly["weather_code"][h0] | 0);
      for (int k = 0; k < 3; ++k) {
        sl.precip += hourly["precipitation"][h0 + k] | 0.0f;
        int pr = hourly["precipitation_probability"][h0 + k] | 0;
        if (pr > sl.prob) sl.prob = pr;
        float wi = hourly["wind_speed_10m"][h0 + k] | 0.0f;
        if (wi > sl.wind) sl.wind = wi;
      }
    }
    if (d < 2) {
      days[d].code = w.code;
      days[d].t_max = w.t_max;
      days[d].t_min = w.t_min;
      days[d].precip = w.precip;
      Serial.printf("Forecast %s %s code %d max %.1f min %.1f rain %.1f mm\n", d == 0 ? "today" : "tomorrow",
                    w.date, w.code, w.t_max, w.t_min, w.precip);
    }
    n++;
  }
  *week_count = n;
  Serial.printf("Weather week %d days, JSON %u bytes, doc %u bytes\n", n, static_cast<unsigned>(payload.length()),
                static_cast<unsigned>(doc.memoryUsage()));
  return n >= 2;
}

const char* kNotConfiguredBody =
    "Wi-Fi and Miniserver settings are missing.\n\n"
    "Copy include/panel_config.example.h\n"
    "to include/panel_config.h, fill in the\n"
    "local Miniserver, then rebuild and\n"
    "flash over USB.\n\n"
    "Relay 1 stays off until you open\n"
    "Sonstiges on the home screen\n"
    "and tap Relais.";

void start_wifi() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setHostname("loxone-panel");
  WiFi.disconnect(false, false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  wifi_started_ms = millis();
}

bool load_structure() {
  uint8_t* buf = static_cast<uint8_t*>(heap_caps_malloc(kStructureCap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (buf == nullptr) {
    ui_show_message("Out of memory", "The panel could not allocate a buffer for LoxAPP3.json.", true);
    return false;
  }

  size_t length = 0;
  LoxoneHttpResult http = loxone_get(*auth, "/data/LoxAPP3.json", buf, kStructureCap - 1, &length);
  if (!http.ok) {
    heap_caps_free(buf);
    ui_show_message("Miniserver unreachable", http.detail, true);
    return false;
  }

  static LoxoneControl controls[kLoxoneControlCap];
  size_t shown = 0;
  size_t supported = 0;
  char server[48];
  char error[120];
  bool parsed = loxone_parse_structure(buf, length, controls, kLoxoneControlCap, &shown, &supported, server,
                                       sizeof(server), error, sizeof(error));
  heap_caps_free(buf);
  if (!parsed) {
    ui_show_message("Could not read Loxone", error, true);
    return false;
  }

  Serial.printf("LoxAPP3 %u bytes, %u controls shown, %u supported\n", static_cast<unsigned>(length),
                static_cast<unsigned>(shown), static_cast<unsigned>(supported));
  ui_show_controls(controls, shown, supported, server[0] != '\0' ? server : "Loxone");
  if (!panel_touch_ok()) {
    ui_set_status("Touch controller not found");
  }
  return true;
}

void poll() {
  switch (phase) {
    case Phase::NotConfigured:
      break;
    case Phase::Wifi: {
      wl_status_t status = WiFi.status();
      if (status == WL_CONNECTED) {
        Serial.printf("Wi-Fi connected, Miniserver %s:%d auth %s\n", LOXONE_HOST, LOXONE_PORT, auth->name());
        phase = Phase::Loading;
        load_painted = false;
        ui_set_status("Wi-Fi connected");
      } else if (status == WL_CONNECT_FAILED || status == WL_NO_SSID_AVAIL ||
                 millis() - wifi_started_ms > kWifiTimeoutMs) {
        phase = Phase::Failed;
        ui_show_message("Wi-Fi failed", "The panel could not join the network in panel_config.h.", true);
      }
      break;
    }
    case Phase::Loading:
      if (!load_painted) {
        ui_show_message("Loading", "Reading /data/LoxAPP3.json from the Miniserver.", false);
        load_painted = true;
        break;
      }
      phase = load_structure() ? Phase::Ready : Phase::Failed;
      break;
    case Phase::Ready: {
      if (WiFi.status() != WL_CONNECTED) {
        ui_set_status("Wi-Fi disconnected");
        break;
      }
      LoxoneCommandRequest command;
      if (ui_take_command(&command)) {
        LoxoneHttpResult result = loxone_send_command(*auth, command.action, command.command);
        Serial.printf("Command %s -> %s\n", command.command, result.ok ? "ok" : result.detail);
        ui_command_finished(command.index, result.ok, command.command, result.detail);
      } else {
        LoxoneStateRequest state;
        if (ui_take_state(&state)) {
          char value[64];
          LoxoneHttpResult result =
              loxone_read_state(*auth, state.action, state.state, state.kind, value, sizeof(value));
          Serial.printf("State %d -> %s (%s)\n", state.index, result.ok ? value : "-",
                        result.ok ? "ok" : result.detail);
          ui_state_finished(state.index, result.ok, result.ok ? value : nullptr);
        } else if (static_cast<int32_t>(millis() - next_weather_ms) >= 0) {
          int temp_c = 0;
          int code = 0;
          WeatherDay days[2] = {};
          int week_count = 0;
          bool ok = fetch_buchs_weather(&temp_c, &code, days, &week_count);
          ui_set_weather(ok, temp_c, code);
          ui_set_forecast(ok, days, 2);
          if (ok) {
            ui_set_week(week, week_count);
          }
          // Every 30 min; retry after 1 min on errors (last forecast stays).
          next_weather_ms = millis() + (ok ? 1800000UL : 60000UL);
          Serial.printf("Weather Buchs %s\n", ok ? "ok" : "unavailable");
        }
      }
      break;
    }
    case Phase::Failed:
      if (ui_take_retry()) {
        start_wifi();
        phase = Phase::Wifi;
        ui_show_message("Connecting", "Joining Wi-Fi.", false);
        ui_set_status("Connecting to Wi-Fi");
      }
      break;
  }
}

}  // namespace

void setup() {
  #ifdef TEST_HARNESS
  Serial.setTxBufferSize(16384);
  Serial.setRxBufferSize(1024);
#endif
  Serial.begin(SERIAL_BAUD);
  delay(200);
  Serial.println("ESP32-4848S040C_I_Y_1 Loxone panel");
  Serial.printf("Flash %u bytes, PSRAM %u bytes\n", static_cast<unsigned>(ESP.getFlashChipSize()),
                static_cast<unsigned>(ESP.getPsramSize()));
  panel_relay_begin();
  Serial.printf("Relay 1 GPIO%d off (level %d). GPIO%d and GPIO%d left unconfigured\n", kPinRelay1,
                kRelayOffLevel, kPinRelay2, kPinRelay3);

  if (!panel_display_begin()) {
    Serial.println("Display init failed");
    while (true) {
      delay(1000);
    }
  }
  panel_touch_begin();
  if (!panel_lvgl_start()) {
    Serial.println("LVGL init failed");
    while (true) {
      delay(1000);
    }
  }
  ui_init();

  if (strcmp(LOXONE_AUTH, "basic") == 0) {
    auth = &basic_auth;
  } else if (strcmp(LOXONE_AUTH, "token") == 0) {
    auth = &token_auth;
  }

  if (!panel_is_configured()) {
    phase = Phase::NotConfigured;
    if (PANEL_CONFIG_PRESENT) {
      Serial.println("panel_config.h is missing Wi-Fi, host, or user");
    } else {
      Serial.println("panel_config.h not found; not configured");
    }
    ui_show_message("Not configured", kNotConfiguredBody, false);
    ui_set_status("Relay off");
    return;
  }

  if (auth == nullptr) {
    phase = Phase::Failed;
    ui_show_message("Not configured", "LOXONE_AUTH must be \"basic\" or \"token\".", false);
    return;
  }

  Serial.printf("Auth scheme %s\n", auth->name());
  start_wifi();
  phase = Phase::Wifi;
  ui_show_message("Connecting", "Joining Wi-Fi.", false);
  ui_set_status("Connecting to Wi-Fi");
}

void loop() {
  static uint32_t last_ms = millis();
  uint32_t now = millis();
  lv_tick_inc(now - last_ms);
  last_ms = now;
  lv_timer_handler();
  harness_poll();
  poll();
  lv_timer_handler();
  delay(5);
}
