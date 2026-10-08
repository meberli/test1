#include <Arduino.h>
#include <WiFi.h>
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

const char* kNotConfiguredBody =
    "Wi-Fi and Miniserver settings are missing.\n\n"
    "Copy include/panel_config.example.h\n"
    "to include/panel_config.h, fill in the\n"
    "local Miniserver, then rebuild and\n"
    "flash over USB.\n\n"
    "Relay 1 stays off until Relais\n"
    "on Sonstiges is tapped.";

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
  Serial.begin(115200);
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
  poll();
  lv_timer_handler();
  delay(5);
}
