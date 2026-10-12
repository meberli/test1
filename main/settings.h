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

// Default location: Buchs SG (the coordinates the firmware always used).
Location settings_default_location();

// Wi-Fi: NVS first, else panel_config.h. false if neither has an SSID.
bool settings_load_wifi(WifiCreds* out);
bool settings_save_wifi(const WifiCreds& creds);

// Location: NVS first, else the default.
Location settings_load_location();
bool settings_save_location(const Location& loc);
bool settings_reset_location();  // back to the default (erases the NVS keys)

// Requests from the settings screens to the network task.
struct SettingsRequest {
  enum Kind : uint8_t { Scan, Connect, Geocode, SetLocation, ResetLocation } kind;
  WifiCreds creds;
  char query[48];
  Location location;
};
