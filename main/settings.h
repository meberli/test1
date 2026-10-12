#pragma once
// User settings kept in NVS (namespace "panel"): Wi-Fi credentials and the
// weather location. Types shared between the UI and the network task.

#include <stddef.h>
#include <stdint.h>

struct WifiCreds {
  char ssid[33];
  char pass[65];
};

struct Location {
  char name[48];
  float lat;
  float lon;
};

struct WifiAp {
  char ssid[33];
  int8_t rssi;
  bool secure;
};

struct GeoPlace {
  char name[48];    // "Zürich"
  char region[48];  // "Zürich, CH"
  float lat;
  float lon;
};

constexpr int kMaxAps = 16;
constexpr int kMaxPlaces = 10;

// Default location: Buchs SG (Open-Meteo geocoding: 47.1674 N, 9.4779 E).
Location settings_default_location();

// Wi-Fi: NVS first, else panel_config.h. false if neither has an SSID.
bool settings_load_wifi(WifiCreds* out);
bool settings_save_wifi(const WifiCreds& creds);

// Location: NVS first, else the default.
Location settings_load_location();
bool settings_save_location(const Location& loc);
bool settings_reset_location();  // back to the default (erases the NVS keys)

// Display: screen timeout in seconds (0 = never) and the off-mode hint.
// saver: what the screen shows after the timeout.
enum Saver : uint8_t { kSaverBlack = 0, kSaverDigital = 1, kSaverAnalog = 2 };
struct DisplaySettings {
  uint16_t timeout_s;
  bool hint;      // kSaverBlack only: dim "wake me up" instead of a dark panel
  uint8_t saver;  // Saver
};
constexpr uint16_t kTimeoutChoices[] = {0, 30, 60, 120, 300, 600};
constexpr int kTimeoutChoiceN = 6;
DisplaySettings settings_load_display();  // default 120 s, hint on, black
bool settings_save_display(const DisplaySettings& d);

// Time: SNTP server and time zone (POSIX TZ string; name is for display).
struct TimeSettings {
  char server[64];
  char tz_name[32];
  char tz[64];
};
// Default: ch.pool.ntp.org, Europe/Zurich (CET-1CEST,M3.5.0,M10.5.0/3).
TimeSettings settings_default_time();
TimeSettings settings_load_time();  // NVS first, else the default
bool settings_save_time(const TimeSettings& t);

// Requests from the settings screens to the network task.
struct SettingsRequest {
  enum Kind : uint8_t { Scan, Connect, Geocode, SetLocation, ResetLocation, SetTime } kind;
  WifiCreds creds;
  char query[48];
  Location location;
  TimeSettings time;
};
