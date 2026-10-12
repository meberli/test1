#pragma once
// Wartung: status values, OTA updates (URL or browser upload) and the small
// maintenance web server (status, settings backup/restore, firmware upload).

#include <stddef.h>
#include <stdint.h>

struct MaintStatus {
  bool wifi;
  int rssi;            // dBm
  char ssid[33];
  char ip[16];
  uint32_t uptime_s;
  uint32_t free_internal;
  uint32_t largest_internal;
  uint32_t free_psram;
  char version[32];    // esp_app_desc version (git describe)
  char built[24];      // date + time of the build
  char idf[16];
  char partition[17];  // running app partition label
  bool ntp_synced;
  uint32_t ntp_age_s;  // seconds since the last sync (0 = never)
  bool web;            // maintenance web server running
};
void maint_collect(MaintStatus* out);  // any task (no LVGL)

// Marks the running image valid (rollback support) - call once the UI runs.
void maint_mark_valid();

// Network task: download and install url, then the caller restarts.
// Progress 0..100 in maint_ota_progress() while it runs.
bool maint_ota_from_url(const char* url, char* err, size_t n);
int maint_ota_progress();

// Maintenance web server on port 80 (core 0). Off by default and after every
// reboot; Einstellungen > Wartung switches it on.
bool maint_web_start();
void maint_web_stop();
bool maint_web_running();
// true once an upload installed a new image (the UI shows it and restarts).
bool maint_restart_pending();
