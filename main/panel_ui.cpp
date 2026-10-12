#include "panel_ui.h"

#include "sys.h"
#include "lvgl.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "fonts.h"
#include "clock_sync.h"
#include <math.h>
#include <sys/time.h>
#include <time.h>
#include "material_icons.h"
#include "settings.h"
#include "panel_hw.h"
#include "weather_derive.h"
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <esp_system.h>

namespace {

constexpr int kScreen = 480;
constexpr int kEdge = 32;
// Touch is sampled every LV_INDEV_DEF_READ_PERIOD ms, so a quick 80 px flick
// can register as ~40 px. Taps stay at <= kTapSlop (12 px).
constexpr int kSwipe = 40;
constexpr int kMargin = 20;
constexpr int kGap = 12;
constexpr int kTileW = 214;
constexpr int kTileH = 156;
constexpr int kContentY = 88;
constexpr int kRowsPerPage = 2;
constexpr int kPerPage = 4;
constexpr int kMaxPages = 32;
constexpr uint32_t kShutterRereadMs = 3000;
constexpr int kLocalRelay = -2;

// Dark Glass (UI proposal 1): deep navy background, glass cards, soft accents.
constexpr uint32_t kBgTop = 0x1A2438;
constexpr uint32_t kBgBottom = 0x0B0F1A;
constexpr uint32_t kText = 0xE8ECF4;
constexpr uint32_t kTextDim = 0x8A94A8;
constexpr uint32_t kCardTop = 0x1B2333;
constexpr uint32_t kCardBottom = 0x131926;
constexpr uint32_t kBorder = 0x2A3448;
constexpr uint32_t kOffIcon = 0x5A6478;
constexpr uint32_t kAccent = 0x7CC4FF;
constexpr uint32_t kAmber = 0xFFC857;
constexpr uint32_t kGreen = 0x7EE0A1;
constexpr uint32_t kBlue = 0x7CC4FF;
constexpr uint32_t kYellow = 0xFFC857;
constexpr uint32_t kOrange = 0xFFD27A;
constexpr uint32_t kCyan = 0x7CC4FF;
constexpr uint32_t kPurple = 0xB4A8FF;
constexpr uint32_t kTeal = 0x7FE3D8;
constexpr uint32_t kRed = 0xFF7A7A;

enum Section : uint8_t { kSecFav = 0, kSecLight = 1, kSecShutter = 2, kSecOther = 3 };
enum class Nav : uint8_t {
  Home, Category, Weather, Settings, WifiList, WifiPass, Location, Display, Time,
  Cameras, CamSettings, CamId, CamSearch, CamKey,
  Transit, Calendar, Rain, TransitSet, StationSearch, CalSet, TextEdit, Maint, RemSet
};

const char* kShutterLabel[2] = {"Auf", "Ab"};
const char* kShutterCommand[2] = {"FullUp", "FullDown"};
const char* kShutterIcon[2] = {ICON_ARROW_UP, ICON_ARROW_DOWN};
const char* kSectionIcon[4] = {ICON_STAR, ICON_BULB, ICON_BLINDS, ICON_TOGGLE};
const uint32_t kSectionAccent[4] = {kAmber, kOrange, kAccent, kGreen};

struct RowPlan {
  bool shutter;
  int a;
  int b;
};

struct PagePlan {
  Section section;
  int nrows;
  RowPlan rows[kRowsPerPage];
};

lv_obj_t* home_root = nullptr;
lv_obj_t* cat_root = nullptr;
lv_obj_t* weather_temp = nullptr;
lv_obj_t* weather_big = nullptr;  // large icon in the weather card
lv_obj_t* home_date = nullptr;
lv_obj_t* home_time = nullptr;
char cur_cond[48] = "Wetter wird geladen";
// Forecast panels inside the weather card: [0] Heute, [1] Morgen.
lv_obj_t* fc_icon[2] = {nullptr, nullptr};
// Weather day detail screen.
lv_obj_t* wx_root = nullptr;
lv_obj_t* wx_title = nullptr;
lv_obj_t* wx_page = nullptr;
lv_obj_t* wx_icon = nullptr;
lv_obj_t* wx_temp = nullptr;
lv_obj_t* wx_cond = nullptr;
lv_obj_t* wx_info[4] = {nullptr, nullptr, nullptr, nullptr};
lv_obj_t* wx_dot[kWxSlots];
lv_obj_t* wx_hour[kWxSlots];
lv_obj_t* wx_sicon[kWxSlots];
lv_obj_t* wx_stemp[kWxSlots];
lv_obj_t* wx_smm[kWxSlots];
lv_obj_t* wx_sprob[kWxSlots];
lv_obj_t* wx_swind[kWxSlots];
lv_obj_t* wx_curve = nullptr;
lv_point_t wx_curve_pts[kWxSlots];
lv_obj_t* wx_prev_hint = nullptr;
lv_obj_t* wx_next_hint = nullptr;
lv_obj_t* wx_empty = nullptr;
WeatherDetailDay* wx_week = nullptr;  // PSRAM copy
int wx_count = 0;
int wx_day = 0;

lv_obj_t* fc_temp[2] = {nullptr, nullptr};
lv_obj_t* fc_rain[2] = {nullptr, nullptr};
bool fc_have = false;
lv_obj_t* card_btn[6] = {};
lv_obj_t* weather_place = nullptr;
// Settings screens
lv_obj_t* set_root = nullptr;
lv_obj_t* set_wifi_val = nullptr;
lv_obj_t* set_loc_val = nullptr;
lv_obj_t* set_disp_val = nullptr;
lv_obj_t* set_time_val = nullptr;
// Uhrzeit (NTP) screen
lv_obj_t* time_root = nullptr;
lv_obj_t* time_srv_btn[4] = {};
lv_obj_t* time_tz_btn[4] = {};
lv_obj_t* time_ta = nullptr;
lv_obj_t* time_status = nullptr;
TimeSettings tset = {};
bool time_saving = false;
struct TzChoice {
  const char* label;
  const char* name;
  const char* posix;
};
const char* kNtpServers[4] = {"ch.pool.ntp.org", "pool.ntp.org", "ntp.metas.ch", "time.google.com"};
const TzChoice kTzChoices[4] = {
    {"Z\xC3\xBCrich", "Europe/Zurich", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"London", "Europe/London", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"UTC", "UTC", "UTC0"},
    {"New York", "America/New_York", "EST5EDT,M3.2.0,M11.1.0"},
};
// Screensaver choices (Bildschirm)
lv_obj_t* disp_saver_btn[3] = {};
// Display settings screen and the screen-off layer
lv_obj_t* disp_root = nullptr;
lv_obj_t* disp_to_btn[kTimeoutChoiceN] = {};
lv_obj_t* disp_hint_sw = nullptr;
DisplaySettings disp = {120, true, kSaverBlack};
uint16_t scr_timeout_s = 120;  // effective timeout (harness SCRTO may override)
bool scr_off = false;
bool wake_swallow = false;
uint32_t last_activity = 0;
lv_obj_t* sleep_layer = nullptr;
lv_obj_t* sleep_hint = nullptr;
lv_obj_t* main_screen = nullptr;
// Clock screensavers (children of sleep_layer, a screen of its own)
lv_obj_t* saver_digital = nullptr;
lv_obj_t* dig_time = nullptr;
lv_obj_t* dig_date = nullptr;
lv_obj_t* saver_analog = nullptr;
lv_timer_t* saver_timer = nullptr;
int saver_last_min = -1;
float sec_angle = -1.f;  // second hand angle drawn last (deg)
lv_area_t sec_area = {};  // its bounding box (absolute coords)
constexpr uint8_t kClockBacklight = 40;  // percent while a clock saver shows
constexpr int kClockD = 440;             // analog clock object size
uint32_t hint_moved_ms = 0;
constexpr uint32_t kHintMoveMs = 45000;
constexpr uint8_t kHintBacklight = 4;  // percent
lv_obj_t* wl_root = nullptr;
lv_obj_t* wl_list = nullptr;
lv_obj_t* wl_status = nullptr;
lv_obj_t* wp_root = nullptr;
lv_obj_t* wp_title = nullptr;
lv_obj_t* wp_ta = nullptr;
lv_obj_t* wp_status = nullptr;
lv_obj_t* loc_root = nullptr;
lv_obj_t* loc_ta = nullptr;
lv_obj_t* loc_list = nullptr;
lv_obj_t* loc_status = nullptr;
lv_obj_t* kb = nullptr;
lv_obj_t* msg_wifi_btn = nullptr;
WifiAp ui_aps[kMaxAps];
int ui_ap_n = 0;
bool wl_scanning = false;
GeoPlace ui_places[kMaxPlaces];
int ui_place_n = 0;
bool loc_searching = false;
bool wp_connecting = false;
char wp_ssid[33] = "";
char cur_ssid[33] = "";
char cur_place[48] = "Buchs SG";
SettingsRequest settings_req;
bool settings_pending = false;

// Kameras (implementation at the end of the namespace).
lv_obj_t* cam_root = nullptr;
lv_obj_t* camset_root = nullptr;
lv_obj_t* camid_root = nullptr;
lv_obj_t* camsearch_root = nullptr;
lv_obj_t* camkey_root = nullptr;
lv_obj_t* set_cam_val = nullptr;
void cam_hide_all();
void cam_enter();
void cam_swipe(int dir);
void cam_tick();
void open_camset();
void build_camera_screens(lv_obj_t* screen);
void camset_value();
bool cam_kb_ready(lv_obj_t* ta);
bool cam_describe(char* out, size_t n);
bool cam_list_dump();
// panel_ui_info.inc (ÖV, Kalender, MeteoSchweiz, Wartung)
lv_obj_t* set_tr_val = nullptr;
lv_obj_t* set_cal_val = nullptr;
lv_obj_t* set_maint_val = nullptr;
lv_obj_t* set_rem_val = nullptr;
lv_obj_t* remset_root = nullptr;
void open_remset();
void rem_tick(bool force);
bool rem_describe(char* out, size_t n);
void build_reminders(lv_obj_t* screen);
bool rem_shown_now();
void info_hide_all();
void info_hide_views();
void home_chips_refresh();
void home_warn_refresh();
void tr_swipe(int dir);
void rain_enter();
void open_trset();
void open_calset();
void open_maint();
void txt_go_back();
void trset_refresh();
void calset_refresh();
void maint_refresh();
bool st_kb_ready(lv_obj_t* ta);
bool txt_kb_ready(lv_obj_t* ta);
bool info_describe(char* out, size_t n);
bool info_list_dump();
void info_tick();
void build_home_info(lv_obj_t* weather_card);
void build_info_screens(lv_obj_t* screen);

// Named widgets for the test harness (POS <name> -> centre coordinates).
struct Named {
  char name[16];
  lv_obj_t* obj;
};
Named named[128];
int named_n = 0;
void reg(const char* name, lv_obj_t* obj) {
  for (int i = 0; i < named_n; ++i) {
    if (strcmp(named[i].name, name) == 0) {
      named[i].obj = obj;
      return;
    }
  }
  if (named_n < 128) {
    snprintf(named[named_n].name, sizeof(named[named_n].name), "%s", name);
    named[named_n].obj = obj;
    named_n++;
  }
}
void unreg_prefix(const char* prefix) {
  size_t n = strlen(prefix);
  for (int i = 0; i < named_n; ++i) {
    if (strncmp(named[i].name, prefix, n) == 0) named[i].obj = nullptr;
  }
}
lv_obj_t* cat_title = nullptr;
lv_obj_t* cat_page = nullptr;
lv_obj_t* prev_hint = nullptr;
lv_obj_t* next_hint = nullptr;
lv_obj_t* cat_status = nullptr;
lv_obj_t* msg_status = nullptr;
lv_obj_t* tiles[kPerPage] = {};
lv_obj_t* tile_name[kPerPage] = {};
lv_obj_t* tile_icon[kPerPage] = {};
lv_obj_t* cat_icon = nullptr;
lv_obj_t* tile_sub[kPerPage] = {};
lv_obj_t* shutter_box[kRowsPerPage] = {};
lv_obj_t* shutter_title[kRowsPerPage] = {};
lv_obj_t* shutter_pos[kRowsPerPage] = {};
lv_obj_t* empty_hint = nullptr;
lv_obj_t* message = nullptr;
lv_obj_t* message_title = nullptr;
lv_obj_t* message_body = nullptr;
lv_obj_t* retry_btn = nullptr;

LoxoneControl controls[kLoxoneControlCap];
size_t control_count = 0;
char server_name[48] = "";
PagePlan pages[kMaxPages];
int page_n = 0;
int section_origin[4] = {-1, -1, -1, -1};
int section_pages[4] = {};
int page = -1;
int section_page = 0;
Section current_section = kSecFav;
Nav nav = Nav::Home;
bool message_open = false;

bool known_on[kLoxoneControlCap] = {};
bool known[kLoxoneControlCap] = {};
bool needs_read[kLoxoneControlCap] = {};
bool prefer[kLoxoneControlCap] = {};
bool moving[kLoxoneControlCap] = {};
int position_pct[kLoxoneControlCap] = {};
uint32_t reread_after[kLoxoneControlCap] = {};
bool command_pending = false;
LoxoneCommandRequest pending = {};
bool retry_pending = false;

bool gesture_consumed = false;
lv_coord_t gesture_x0 = 0;
lv_coord_t gesture_y0 = 0;

lv_obj_t* make_label(lv_obj_t* parent, const lv_font_t* font, lv_color_t color) {
  lv_obj_t* label = lv_label_create(parent);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, color, 0);
  lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
  return label;
}

void hide(lv_obj_t* obj) {
  if (obj != nullptr) {
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  }
}

void show(lv_obj_t* obj) {
  if (obj != nullptr) {
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
  }
}

// Glass card: dark navy gradient with a thin light border.
void style_glass(lv_obj_t* obj, lv_coord_t radius) {
  lv_obj_set_style_bg_color(obj, lv_color_hex(kCardTop), 0);
  lv_obj_set_style_bg_grad_color(obj, lv_color_hex(kCardBottom), 0);
  lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(obj, lv_color_hex(0x2C3954), LV_STATE_PRESSED);
  lv_obj_set_style_bg_grad_color(obj, lv_color_hex(0x1F2940), LV_STATE_PRESSED);
  lv_obj_set_style_radius(obj, radius, 0);
  lv_obj_set_style_border_width(obj, 1, 0);
  lv_obj_set_style_border_color(obj, lv_color_hex(kBorder), 0);
  lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);
  lv_obj_set_style_shadow_width(obj, 0, 0);
  lv_obj_set_style_outline_width(obj, 0, 0);
  lv_obj_set_style_pad_all(obj, 0, 0);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

// Checked state (selected choice): accent-tinted glass with an accent border.
void style_checked(lv_obj_t* obj) {
  lv_obj_set_style_bg_color(obj, lv_color_hex(0x2B4A6B), LV_STATE_CHECKED);
  lv_obj_set_style_bg_grad_color(obj, lv_color_hex(0x1F3550), LV_STATE_CHECKED);
  lv_obj_set_style_border_color(obj, lv_color_hex(kAccent), LV_STATE_CHECKED);
}

// Home button: glass squircle with a coloured icon. The tap flash
// (LV_STATE_PRESSED) tints it with the accent.
void style_tile(lv_obj_t* obj, uint32_t accent) {
  lv_color_t a = lv_color_hex(accent);
  style_glass(obj, 24);
  lv_obj_set_style_bg_color(obj, lv_color_mix(a, lv_color_hex(kCardTop), 90), LV_STATE_PRESSED);
  lv_obj_set_style_bg_grad_color(obj, lv_color_mix(a, lv_color_hex(kCardBottom), 50), LV_STATE_PRESSED);
  lv_obj_set_style_border_color(obj, a, LV_STATE_PRESSED);
}

void style_plain(lv_obj_t* obj) {
  lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(obj, 0, 0);
  lv_obj_set_style_shadow_width(obj, 0, 0);
  lv_obj_set_style_pad_all(obj, 0, 0);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
}

const char* section_name(Section section) {
  switch (section) {
    case kSecFav:
      return "Favoriten";
    case kSecLight:
      return "Licht";
    case kSecShutter:
      return "Storen";
    default:
      return "Schalter";
  }
}

const char* empty_text(Section section) {
  switch (section) {
    case kSecFav:
      return "Keine Favoriten";
    case kSecLight:
      return "Keine Lichter";
    case kSecShutter:
      return "Keine Storen";
    default:
      return "Keine weiteren Steuerungen";
  }
}

int round_c(float v) { return static_cast<int>(v >= 0.f ? v + 0.5f : v - 0.5f); }

// --- Weather icons: Material Symbols glyphs, by WMO weather code ----------
enum class Sky { None, Sun, Partly, Mostly, Cloud, Showers, Rain, Snow, Thunder, Fog };

Sky sky_for(int code) {
  if (code < 0) return Sky::None;
  if (code == 0) return Sky::Sun;
  if (code <= 2) return Sky::Partly;
  if (code == 3) return Sky::Cloud;
  if (code == 45 || code == 48) return Sky::Fog;
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return Sky::Snow;
  if (code >= 95) return Sky::Thunder;
  if (code >= 80 && code <= 82) return Sky::Showers;
  if (code >= 51 && code <= 67) return Sky::Rain;
  return Sky::Cloud;
}

Sky sky_for_day(int kind) {
  switch (static_cast<DayKind>(kind)) {
    case DayKind::Sunny: return Sky::Sun;
    case DayKind::Partly: return Sky::Partly;
    case DayKind::MostlyCloudy: return Sky::Mostly;
    case DayKind::Overcast: return Sky::Cloud;
    case DayKind::Fog: return Sky::Fog;
    case DayKind::Showers: return Sky::Showers;
    case DayKind::Rain: return Sky::Rain;
    case DayKind::SnowShowers:
    case DayKind::Snow: return Sky::Snow;
    case DayKind::Thunder: return Sky::Thunder;
  }
  return Sky::Cloud;
}

struct Glyph {
  const char* text;
  uint32_t color;
};

Glyph glyph_for(Sky sky) {
  switch (sky) {
    case Sky::None: return {"", kTextDim};
    case Sky::Sun: return {ICON_W_SUN, kAmber};
    case Sky::Partly: return {ICON_W_PARTLY, kAmber};
    case Sky::Mostly: return {ICON_W_CLOUD, 0xC4CCDC};
    case Sky::Cloud: return {ICON_W_CLOUD, 0x9AA4B8};
    case Sky::Showers: return {ICON_W_SHOWERS, kAccent};
    case Sky::Rain: return {ICON_W_RAIN, kAccent};
    case Sky::Snow: return {ICON_W_SNOW, kText};
    case Sky::Thunder: return {ICON_W_THUNDER, kAmber};
    case Sky::Fog: return {ICON_W_FOG, 0xA8B2C6};
  }
  return {ICON_W_CLOUD, kTextDim};
}

void set_glyph(lv_obj_t* label, Glyph g) {
  lv_label_set_text(label, g.text);
  lv_obj_set_style_text_color(label, lv_color_hex(g.color), 0);
}

// label is an lv_label with one of the icon fonts.
void weather_icon(lv_obj_t* label, int code) { set_glyph(label, glyph_for(sky_for(code))); }
void day_icon(lv_obj_t* label, int kind) { set_glyph(label, glyph_for(sky_for_day(kind))); }

// "Buchs SG · Teilweise bewölkt" under the temperature.
void update_weather_line() {
  if (weather_place == nullptr) return;
  char buf[100];
  snprintf(buf, sizeof(buf), "%s \xC2\xB7 %s", cur_place, cur_cond);
  lv_label_set_text(weather_place, buf);
}

const char* weather_name(int code) {
  switch (code) {
    case 0:
      return "Klar";
    case 1:
      return "Heiter";
    case 2:
      return "Bewölkt";
    case 3:
      return "Bedeckt";
    case 45:
    case 48:
      return "Nebel";
    case 51:
    case 53:
    case 55:
    case 56:
    case 57:
      return "Niesel";
    case 61:
    case 63:
    case 65:
    case 66:
    case 67:
      return "Regen";
    case 71:
    case 73:
    case 75:
    case 77:
      return "Schnee";
    case 80:
    case 81:
    case 82:
      return "Regenschauer";
    case 85:
    case 86:
      return "Schneeschauer";
    case 95:
    case 96:
    case 99:
      return "Gewitter";
    default:
      return "Wetter";
  }
}

Section section_of(const LoxoneControl& ctrl) {
  if (ctrl.favorite) {
    return kSecFav;
  }
  if (ctrl.kind == kLoxoneSwitch) {
    return kSecLight;
  }
  if (ctrl.kind == kLoxoneShutter) {
    return kSecShutter;
  }
  return kSecOther;
}

bool has_status(const LoxoneControl& ctrl) {
  return ctrl.kind == kLoxoneSwitch || ctrl.kind == kLoxoneShutter;
}

int compare_index(const void* left, const void* right) {
  int a = *static_cast<const int*>(left);
  int b = *static_cast<const int*>(right);
  int room = strcasecmp(controls[a].room, controls[b].room);
  if (room != 0) {
    return room;
  }
  int shutter_a = controls[a].kind == kLoxoneShutter ? 1 : 0;
  int shutter_b = controls[b].kind == kLoxoneShutter ? 1 : 0;
  if (shutter_a != shutter_b) {
    return shutter_a - shutter_b;
  }
  return strcasecmp(controls[a].name, controls[b].name);
}

void pack_indices(Section section, int* idxs, int n) {
  if (n <= 0 || page_n >= kMaxPages) {
    return;
  }
  qsort(idxs, static_cast<size_t>(n), sizeof(int), compare_index);
  PagePlan* current = &pages[page_n++];
  current->section = section;
  current->nrows = 0;
  current->rows[0] = RowPlan{false, -1, -1};
  current->rows[1] = RowPlan{false, -1, -1};
  int i = 0;
  while (i < n) {
    if (current->nrows == kRowsPerPage) {
      if (page_n >= kMaxPages) {
        return;
      }
      current = &pages[page_n++];
      current->section = section;
      current->nrows = 0;
      current->rows[0] = RowPlan{false, -1, -1};
      current->rows[1] = RowPlan{false, -1, -1};
    }
    RowPlan* row = &current->rows[current->nrows];
    if (controls[idxs[i]].kind == kLoxoneShutter) {
      row->shutter = true;
      row->a = idxs[i];
      row->b = -1;
      current->nrows++;
      i++;
      continue;
    }
    row->shutter = false;
    row->a = idxs[i];
    row->b = -1;
    i++;
    if (i < n && controls[idxs[i]].kind != kLoxoneShutter) {
      row->b = idxs[i];
      i++;
    }
    current->nrows++;
  }
}

void add_empty_page(Section section) {
  if (page_n >= kMaxPages) {
    return;
  }
  PagePlan* current = &pages[page_n++];
  current->section = section;
  current->nrows = 0;
  current->rows[0] = RowPlan{false, -1, -1};
  current->rows[1] = RowPlan{false, -1, -1};
}

bool has_section(Section section) {
  for (int i = 0; i < page_n; ++i) {
    if (pages[i].section == section) {
      return true;
    }
  }
  return false;
}

void pack_other(int* idxs, int n) {
  if (n > 1) {
    qsort(idxs, static_cast<size_t>(n), sizeof(int), compare_index);
  }
  if (page_n >= kMaxPages) {
    return;
  }
  PagePlan* current = &pages[page_n++];
  current->section = kSecOther;
  current->nrows = 1;
  current->rows[0] = RowPlan{false, kLocalRelay, -1};
  current->rows[1] = RowPlan{false, -1, -1};
  int i = 0;
  if (i < n) {
    current->rows[0].b = idxs[i++];
  }
  while (i < n) {
    if (current->nrows == kRowsPerPage) {
      if (page_n >= kMaxPages) {
        return;
      }
      current = &pages[page_n++];
      current->section = kSecOther;
      current->nrows = 0;
      current->rows[0] = RowPlan{false, -1, -1};
      current->rows[1] = RowPlan{false, -1, -1};
    }
    RowPlan* row = &current->rows[current->nrows];
    row->shutter = false;
    row->a = idxs[i++];
    row->b = -1;
    if (i < n) {
      row->b = idxs[i++];
    }
    current->nrows++;
  }
}

void index_sections() {
  for (int s = 0; s < 4; ++s) {
    section_origin[s] = -1;
    section_pages[s] = 0;
  }
  for (int i = 0; i < page_n; ++i) {
    int s = static_cast<int>(pages[i].section);
    if (section_origin[s] < 0) {
      section_origin[s] = i;
    }
    section_pages[s]++;
  }
}

void rebuild_pages() {
  page_n = 0;
  for (int section = kSecFav; section <= kSecShutter; ++section) {
    int idxs[kLoxoneControlCap];
    int n = 0;
    for (size_t i = 0; i < control_count; ++i) {
      if (section_of(controls[i]) == static_cast<Section>(section)) {
        idxs[n++] = static_cast<int>(i);
      }
    }
    pack_indices(static_cast<Section>(section), idxs, n);
    if (!has_section(static_cast<Section>(section))) {
      add_empty_page(static_cast<Section>(section));
    }
  }
  int idxs[kLoxoneControlCap];
  int n = 0;
  for (size_t i = 0; i < control_count; ++i) {
    if (section_of(controls[i]) == kSecOther) {
      idxs[n++] = static_cast<int>(i);
    }
  }
  pack_other(idxs, n);
  index_sections();
}

bool index_on_page(int index) {
  if (page < 0 || page >= page_n) {
    return false;
  }
  const PagePlan& current = pages[page];
  for (int row = 0; row < current.nrows; ++row) {
    if (current.rows[row].a == index || current.rows[row].b == index) {
      return true;
    }
  }
  return false;
}

bool visible_pending() {
  for (size_t i = 0; i < control_count; ++i) {
    if (needs_read[i] && reread_after[i] == 0 && index_on_page(static_cast<int>(i))) {
      return true;
    }
  }
  return false;
}

void publish_status() {
  if (visible_pending()) {
    ui_set_status("Aktualisiere...");
    return;
  }
  for (size_t i = 0; i < control_count; ++i) {
    if (index_on_page(static_cast<int>(i)) && has_status(controls[i]) && !known[i]) {
      ui_set_status("Status nicht verfügbar");
      return;
    }
  }
  ui_set_status(server_name);
}

void arm_visible_reads() {
  for (size_t i = 0; i < control_count; ++i) {
    prefer[i] = false;
    if (!index_on_page(static_cast<int>(i)) || !has_status(controls[i])) {
      continue;
    }
    if (reread_after[i] == 0) {
      needs_read[i] = true;
    }
    prefer[i] = true;
  }
}

// Control tile: glass; "on" glows amber (icon, state text, border).
void style_control(int slot, LoxoneKind kind, bool is_known, bool on, const char* icon) {
  lv_obj_t* btn = tiles[slot];
  style_glass(btn, 22);
  bool lit = is_known && on;
  if (lit) {
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x3A3220), 0);
    lv_obj_set_style_bg_grad_color(btn, lv_color_hex(0x1F1C16), 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(kAmber), 0);
    lv_obj_set_style_border_opa(btn, LV_OPA_40, 0);
  }
  uint32_t ic = lit ? kAmber : kind == kLoxonePulse ? kAccent : kOffIcon;
  lv_label_set_text(tile_icon[slot], icon);
  lv_obj_set_style_text_color(tile_icon[slot], lv_color_hex(ic), 0);
  lv_obj_set_style_text_color(tile_sub[slot], lv_color_hex(lit ? kAmber : kTextDim), 0);
}

void format_switch_sub(char* out, size_t n, int index) {
  const LoxoneControl& ctrl = controls[index];
  const char* mark = "";
  if (known[index]) {
    mark = known_on[index] ? "An" : "Aus";
  } else if (needs_read[index]) {
    mark = "...";
  }
  if (ctrl.room[0] != '\0' && mark[0] != '\0') {
    snprintf(out, n, "%s · %s", ctrl.room, mark);
  } else if (ctrl.room[0] != '\0') {
    snprintf(out, n, "%s", ctrl.room);
  } else if (mark[0] != '\0') {
    snprintf(out, n, "%s", mark);
  } else if (ctrl.kind == kLoxonePulse) {
    snprintf(out, n, "Taster");
  } else {
    snprintf(out, n, "Licht");
  }
}

void format_shutter_pos(char* out, size_t n, int index) {
  if (moving[index]) {
    snprintf(out, n, "Bewegt");
    return;
  }
  if (!known[index]) {
    snprintf(out, n, "%s", needs_read[index] ? "..." : "");
    return;
  }
  int pct = position_pct[index];
  if (pct <= 2) {
    snprintf(out, n, "Offen");
  } else if (pct >= 98) {
    snprintf(out, n, "Geschlossen");
  } else {
    snprintf(out, n, "%d%%", pct);
  }
}

void fill_title(char* out, size_t n, const LoxoneControl& ctrl) {
  // "Room · Name", but only the name when both are the same
  // (e.g. a shutter called "Gästezimmer" in room "Gästezimmer").
  if (ctrl.room[0] != '\0' && strcasecmp(ctrl.room, ctrl.name) != 0) {
    snprintf(out, n, "%s · %s", ctrl.room, ctrl.name);
  } else {
    snprintf(out, n, "%s", ctrl.name);
  }
}

void show_relay_tile(int slot) {
  show(tiles[slot]);
  bool on = panel_relay_is_on();
  lv_label_set_text(tile_name[slot], "Relais");
  lv_label_set_text(tile_sub[slot], on ? "An" : "Aus");
  style_control(slot, kLoxoneSwitch, true, on, on ? ICON_TOGGLE : ICON_TOGGLE_OFF);
}

void show_switch_tile(int slot, int index) {
  if (index == kLocalRelay) {
    show_relay_tile(slot);
    return;
  }
  show(tiles[slot]);
  const LoxoneControl& ctrl = controls[index];
  lv_label_set_text(tile_name[slot], ctrl.name);
  char sub[96];
  format_switch_sub(sub, sizeof(sub), index);
  lv_label_set_text(tile_sub[slot], sub);
  bool on = known[index] && known_on[index];
  bool light = current_section == kSecLight || ctrl.state_kind == kLoxoneStatePosition ||
               ctrl.state_kind == kLoxoneStateScene;
  const char* icon = light ? ICON_BULB : ctrl.kind == kLoxonePulse ? ICON_TOGGLE : on ? ICON_TOGGLE : ICON_TOGGLE_OFF;
  style_control(slot, ctrl.kind, known[index], known_on[index], icon);
}

void show_shutter_row(int row_slot, int index) {
  show(shutter_box[row_slot]);
  char title[110];
  char pos[16];
  fill_title(title, sizeof(title), controls[index]);
  format_shutter_pos(pos, sizeof(pos), index);
  lv_label_set_text(shutter_title[row_slot], title);
  lv_label_set_text(shutter_pos[row_slot], pos);
}

void fill_home_cards() {}  // the home buttons show no counts any more

void refresh_category() {
  if (section_pages[current_section] <= 0 || section_origin[current_section] < 0) {
    return;
  }
  int count = section_pages[current_section];
  if (section_page >= count) {
    section_page = count - 1;
  }
  if (section_page < 0) {
    section_page = 0;
  }
  page = section_origin[current_section] + section_page;
  // Left-edge handle: a previous page exists (swipe right to reach it).
  if (prev_hint != nullptr) {
    if (section_page > 0) {
      show(prev_hint);
    } else {
      hide(prev_hint);
    }
  }
  // Right-edge handle: a next page exists (swipe left to reach it).
  if (next_hint != nullptr) {
    if (section_page + 1 < count) {
      show(next_hint);
    } else {
      hide(next_hint);
    }
  }
  for (int slot = 0; slot < kPerPage; ++slot) {
    hide(tiles[slot]);
  }
  for (int row = 0; row < kRowsPerPage; ++row) {
    hide(shutter_box[row]);
  }
  lv_label_set_text(cat_title, section_name(current_section));
  lv_label_set_text(cat_icon, kSectionIcon[current_section]);
  lv_obj_set_style_text_color(cat_icon, lv_color_hex(kSectionAccent[current_section]), 0);
  if (count > 1) {
    char text[16];
    snprintf(text, sizeof(text), "%d / %d", section_page + 1, count);
    lv_label_set_text(cat_page, text);
    show(cat_page);
  } else {
    hide(cat_page);
  }
  const PagePlan& current = pages[page];
  if (current.nrows == 0) {
    lv_label_set_text(empty_hint, empty_text(current.section));
    show(empty_hint);
  } else {
    hide(empty_hint);
    for (int row_slot = 0; row_slot < current.nrows; ++row_slot) {
      if (current.rows[row_slot].shutter) {
        show_shutter_row(row_slot, current.rows[row_slot].a);
        continue;
      }
      if (current.rows[row_slot].a >= 0 || current.rows[row_slot].a == kLocalRelay) {
        show_switch_tile(row_slot * 2, current.rows[row_slot].a);
      }
      if (current.rows[row_slot].b >= 0 || current.rows[row_slot].b == kLocalRelay) {
        show_switch_tile(row_slot * 2 + 1, current.rows[row_slot].b);
      }
    }
  }
}

void hide_settings() {
  hide(set_root);
  hide(wl_root);
  hide(wp_root);
  hide(loc_root);
  hide(disp_root);
  hide(time_root);
  cam_hide_all();
  info_hide_all();
  if (kb != nullptr) {
    lv_keyboard_set_textarea(kb, nullptr);
    hide(kb);
  }
}

void open_settings();
void settings_back();

void show_home() {
  nav = Nav::Home;
  page = -1;
  hide(cat_root);
  hide(wx_root);
  hide(cam_root);
  hide(message);
  hide_settings();
  info_hide_views();
  show(home_root);
  fill_home_cards();
  home_chips_refresh();
  home_warn_refresh();
  message_open = false;
}

void enter_section(Section section) {
  if (section_pages[section] <= 0) {
    return;
  }
  nav = Nav::Category;
  current_section = section;
  section_page = 0;
  message_open = false;
  hide(message);
  hide(home_root);
  hide(wx_root);
  info_hide_views();
  show(cat_root);
  refresh_category();
  arm_visible_reads();
  publish_status();
}

void go_home() {
  if (message_open) {
    return;
  }
  show_home();
}

void back_level() {
  if (nav == Nav::Category) {
    go_home();
  }
}

void refresh_weather_day();
void open_weather_day(int day);

void swipe_page(int dir) {
  if (nav == Nav::Cameras) {
    cam_swipe(dir);
    return;
  }
  if (nav == Nav::Transit) {
    tr_swipe(dir);
    return;
  }
  if (nav == Nav::Rain) {
    if (dir > 0) open_weather_day(0);
    return;
  }
  if (nav == Nav::TransitSet || nav == Nav::CalSet || nav == Nav::Maint || nav == Nav::RemSet) {
    if (dir < 0) settings_back();
    return;
  }
  if (nav == Nav::StationSearch) {
    if (dir < 0) open_trset();
    return;
  }
  if (nav == Nav::TextEdit) {
    if (dir < 0) txt_go_back();
    return;
  }
  if (nav == Nav::CamId || nav == Nav::CamSearch || nav == Nav::CamKey) {
    if (dir < 0) open_camset();
    return;
  }
  if (nav == Nav::CamSettings) {
    if (dir < 0) settings_back();
    return;
  }
  if (nav == Nav::WifiList || nav == Nav::WifiPass || nav == Nav::Location || nav == Nav::Display ||
      nav == Nav::Time) {
    if (dir < 0) settings_back();
    return;
  }
  if (nav == Nav::Weather) {
    if (wx_day == 0 && dir < 0) {  // before "Heute": warnings and the rain nowcast
      rain_enter();
      return;
    }
    int next = wx_day + dir;
    if (next >= 0 && next < wx_count) {
      wx_day = next;
      refresh_weather_day();
    }
    return;
  }
  if (nav != Nav::Category) {
    return;
  }
  int count = section_pages[current_section];
  int next = section_page + dir;
  if (next < 0 || next >= count) {
    return;
  }
  section_page = next;
  refresh_category();
  arm_visible_reads();
  publish_status();
}

// Tap vs swipe. Every touch is tracked from finger-down to finger-up in the
// input read path (ui_touch_sample), not per widget: LVGL hands a press to
// the next widget when the finger slides onto it, so per-widget PRESSED
// restarted the swipe and the release then clicked that widget.
// A touch is a tap only if it never moved more than kTapSlop px from where it
// started and lasted at most kTapMaxMs. Anything else is a gesture and no
// widget handler acts on it (gesture_consumed). Swipes need kSwipe px.
constexpr int kTapSlop = 12;
constexpr uint32_t kTapMaxMs = 1000;

bool touch_down = false;
uint32_t touch_t0 = 0;
int touch_max_move = 0;
lv_coord_t touch_x = 0;
lv_coord_t touch_y = 0;

void run_gesture(void* param) {
  intptr_t g = reinterpret_cast<intptr_t>(param);
  if (message_open) {
    return;
  }
  if (g == 1) {
    go_home();
  } else if (g == 2) {
    back_level();
  } else if (g == 3) {
    swipe_page(1);
  } else if (g == 4) {
    swipe_page(-1);
  }
}

void finish_touch() {
  int dx = touch_x - gesture_x0;
  int dy = touch_y - gesture_y0;
  int adx = abs(dx);
  int ady = abs(dy);
  bool tap = touch_max_move <= kTapSlop && now_ms() - touch_t0 <= kTapMaxMs;
  gesture_consumed = !tap;
  intptr_t g = 0;
  if (adx >= kSwipe || ady >= kSwipe) {
    // Home is the only edge gesture: swipe up from the bottom edge. There is
    // no right-edge "back" any more: a left swipe that starts near the right
    // edge (where a thumb naturally starts) used to go home instead of
    // paging. Horizontal swipes page from any start position.
    if (gesture_y0 >= kScreen - kEdge && dy <= -kSwipe && ady > adx) {
      g = 1;
    } else if ((nav == Nav::Weather || nav == Nav::Cameras || nav == Nav::Transit) && dy <= -kSwipe && ady > adx + 8) {
      g = 1;  // the day view has no vertical use, so any upward swipe goes home
    } else if (adx >= kSwipe && adx > ady + 8) {
      g = dx < 0 ? 3 : 4;
    }
  }
  console_printf("Touch %s from %d,%d to %d,%d dx=%d dy=%d move=%d %ums%s\n", tap ? "tap" : "gesture",
                static_cast<int>(gesture_x0), static_cast<int>(gesture_y0), static_cast<int>(touch_x),
                static_cast<int>(touch_y), dx, dy, touch_max_move,
                static_cast<unsigned>(now_ms() - touch_t0),
                g == 1 ? " home" : g == 3 ? " next" : g == 4 ? " prev" : "");
  if (g != 0) {
    // Not from inside the input read; let LVGL finish this release first.
    lv_async_call(run_gesture, reinterpret_cast<void*>(g));
  }
}

// Press feedback only for taps. LVGL sets LV_STATE_PRESSED the moment a
// finger lands (and again on every widget it slides onto), so swipes lit up
// tiles. Every interactive widget drops that state right away and instead
// flashes it for kFlashMs once the touch has been classified as a tap
// (CLICKED with !gesture_consumed). No group is used, so no focus lingers.
constexpr uint32_t kFlashMs = 120;

void clear_flash(lv_timer_t* timer) {
  lv_obj_t* obj = static_cast<lv_obj_t*>(timer->user_data);
  // The widget may be gone (a list rebuilt by its own click handler).
  if (lv_obj_is_valid(obj)) lv_obj_clear_state(obj, LV_STATE_PRESSED);
  // repeat count 1: LVGL deletes the timer itself after this call (an extra
  // lv_timer_del here freed it twice and could take other timers with it).
}

void on_press_visual(lv_event_t* event) {
  lv_obj_t* obj = lv_event_get_target(event);
  lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
    lv_obj_clear_state(obj, LV_STATE_PRESSED);
  } else if (code == LV_EVENT_CLICKED && !gesture_consumed && !lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) {
    lv_obj_add_state(obj, LV_STATE_PRESSED);
    lv_timer_t* timer = lv_timer_create(clear_flash, kFlashMs, obj);
    lv_timer_set_repeat_count(timer, 1);
  }
}

void watch(lv_obj_t* obj) {
  lv_obj_add_event_cb(obj, on_press_visual, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(obj, on_press_visual, LV_EVENT_PRESSING, nullptr);
  lv_obj_add_event_cb(obj, on_press_visual, LV_EVENT_CLICKED, nullptr);
}

int control_for_slot(int slot) {
  if (page < 0 || page >= page_n) {
    return -1;
  }
  int row_slot = slot / 2;
  int col = slot % 2;
  const PagePlan& current = pages[page];
  if (row_slot < 0 || row_slot >= current.nrows || current.rows[row_slot].shutter) {
    return -1;
  }
  return col == 0 ? current.rows[row_slot].a : current.rows[row_slot].b;
}

void queue_command(int index, const char* command) {
  snprintf(pending.action, sizeof(pending.action), "%s", controls[index].action);
  snprintf(pending.command, sizeof(pending.command), "%s", command);
  pending.index = index;
  command_pending = true;
  if (!visible_pending()) {
    ui_set_status("Sende...");
  }
}

void on_tile(lv_event_t* event) {
  if (gesture_consumed) {
    return;
  }
  int slot = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
  int index = control_for_slot(slot);
  console_printf("Activate tile slot %d control %d\n", slot, index);
  if (index == kLocalRelay) {
    panel_relay_set(!panel_relay_is_on());
    console_printf("Relay 1 %s\n", panel_relay_is_on() ? "on" : "off");
    refresh_category();
    return;
  }
  if (command_pending) {
    return;
  }
  if (index < 0 || static_cast<size_t>(index) >= control_count) {
    return;
  }
  const LoxoneControl& ctrl = controls[index];
  const char* command = "Pulse";
  if (ctrl.kind == kLoxoneSwitch) {
    command = (known[index] && known_on[index]) ? "Off" : "On";
  }
  queue_command(index, command);
  lv_label_set_text(tile_sub[slot], "Sende...");
}

void on_shutter(lv_event_t* event) {
  if (gesture_consumed || command_pending) {
    return;
  }
  intptr_t tag = reinterpret_cast<intptr_t>(lv_event_get_user_data(event));
  int row_slot = static_cast<int>(tag / 2);
  int which = static_cast<int>(tag % 2);
  if (page < 0 || page >= page_n || row_slot < 0 || row_slot >= kRowsPerPage || which < 0 || which > 1) {
    return;
  }
  const PagePlan& current = pages[page];
  if (row_slot >= current.nrows || !current.rows[row_slot].shutter) {
    return;
  }
  console_printf("Activate shutter row %d %s\n", row_slot, kShutterCommand[which]);
  queue_command(current.rows[row_slot].a, kShutterCommand[which]);
  lv_label_set_text(shutter_pos[row_slot], "Sende...");
}

void on_category(lv_event_t* event) {
  if (gesture_consumed) {
    return;
  }
  int which = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
  if (which == 4) {
    console_printf("Activate settings\n");
    open_settings();
    return;
  }
  if (which == 5) {
    console_printf("Activate cameras\n");
    cam_enter();
    return;
  }
  if (which < kSecFav || which > kSecOther) {
    return;
  }
  console_printf("Activate category %d\n", which);
  enter_section(static_cast<Section>(which));
}

void open_weather_day(int day) {
  if (wx_root == nullptr || message_open) {
    return;
  }
  nav = Nav::Weather;
  wx_day = day < 0 ? 0 : day;
  hide(home_root);
  hide(cat_root);
  info_hide_views();
  show(wx_root);
  refresh_weather_day();
}

void on_day_panel(lv_event_t* event) {
  if (gesture_consumed) {
    return;
  }
  int day = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
  console_printf("Activate weather day %d\n", day);
  open_weather_day(day);
}

void on_msg_wifi(lv_event_t*);

void on_retry(lv_event_t*) {
  if (gesture_consumed) {
    return;
  }
  retry_pending = true;
}

void style_shutter_button(lv_obj_t* btn) {
  style_glass(btn, 18);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x232D42), 0);
  lv_obj_set_style_bg_grad_color(btn, lv_color_hex(0x1A2234), 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x2B4A6B), LV_STATE_PRESSED);
  lv_obj_set_style_border_color(btn, lv_color_hex(kAccent), LV_STATE_PRESSED);
}

bool apply_state_value(int index, const char* value) {
  if (value == nullptr || value[0] == '\0') {
    return false;
  }
  char* end = nullptr;
  float parsed = strtof(value, &end);
  if (end == value) {
    return false;
  }
  const LoxoneControl& ctrl = controls[index];
  if (ctrl.kind == kLoxoneShutter) {
    float frac = parsed;
    if (parsed > 1.f) {
      frac = parsed / 100.f;
    }
    if (frac < 0.f) {
      frac = 0.f;
    }
    if (frac > 1.f) {
      frac = 1.f;
    }
    position_pct[index] = static_cast<int>(frac * 100.f + 0.5f);
    known_on[index] = position_pct[index] >= 98;
    known[index] = true;
    return true;
  }
  known_on[index] = parsed != 0.f;
  known[index] = true;
  return true;
}

bool waiting_for_later(int index, uint32_t now) {
  return reread_after[index] != 0 && static_cast<int32_t>(now - reread_after[index]) < 0;
}

int pick_state(bool preferred_only, uint32_t now) {
  for (size_t i = 0; i < control_count; ++i) {
    if (!needs_read[i] || waiting_for_later(static_cast<int>(i), now)) {
      continue;
    }
    if (preferred_only && !prefer[i]) {
      continue;
    }
    return static_cast<int>(i);
  }
  return -1;
}

void refresh_visible() {
  if (message_open) {
    return;
  }
  if (nav == Nav::Category) {
    refresh_category();
  } else {
    fill_home_cards();
  }
}

// ---- Weather day detail ---------------------------------------------------
const char* kWeekday[7] = {"Sonntag", "Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag", "Samstag"};
const char* kWeekdayShort[7] = {"So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};
const char* kMonth[12] = {"Januar", "Februar", "M\xC3\xA4rz", "April", "Mai", "Juni",
                          "Juli", "August", "September", "Oktober", "November", "Dezember"};
const char* kMonthShort[12] = {"Jan", "Feb", "M\xC3\xA4r", "Apr", "Mai", "Jun", "Jul", "Aug", "Sep", "Okt", "Nov", "Dez"};

int weekday_of(int y, int m, int d) {  // 0 = Sunday (Sakamoto)
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) y -= 1;
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

lv_obj_t* wx_label(lv_obj_t* parent, const lv_font_t* font, uint32_t color, int x, int y) {
  lv_obj_t* l = make_label(parent, font, lv_color_hex(color));
  lv_obj_set_pos(l, x, y);
  return l;
}

lv_obj_t* make_bar(lv_obj_t* parent, int x, int y, int w, int h, uint32_t color, lv_opa_t opa) {
  lv_obj_t* b = lv_obj_create(parent);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_size(b, w, h);
  lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(b, opa, 0);
  lv_obj_set_style_border_width(b, 0, 0);
  lv_obj_set_style_pad_all(b, 0, 0);
  lv_obj_clear_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
  return b;
}

// Edge handle: a next/previous page exists.
lv_obj_t* wx_handle(lv_obj_t* parent, int x) { return make_bar(parent, x, 190, 5, 80, kAccent, LV_OPA_50); }

// Home bar at the bottom: swipe up from the bottom edge goes home.
void make_home_bar(lv_obj_t* parent) { make_bar(parent, 200, 468, 80, 4, 0xFFFFFF, LV_OPA_30); }

lv_obj_t* make_divider(lv_obj_t* parent, int x, int y, int w, int h) {
  lv_obj_t* d = make_bar(parent, x, y, w, h, kBorder, LV_OPA_COVER);
  lv_obj_set_style_radius(d, 0, 0);
  return d;
}

constexpr int kColW = 55;
constexpr int kCurveTop = 66;
constexpr int kCurveH = 40;

void build_weather_screen(lv_obj_t* screen) {
  wx_root = lv_obj_create(screen);
  lv_obj_set_pos(wx_root, 0, 0);
  lv_obj_set_size(wx_root, kScreen, kScreen);
  style_plain(wx_root);

  wx_title = wx_label(wx_root, &font_inter_14, kTextDim, 22, 20);
  lv_obj_set_width(wx_title, 380);
  lv_label_set_long_mode(wx_title, LV_LABEL_LONG_DOT);
  wx_page = make_label(wx_root, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_align(wx_page, LV_ALIGN_TOP_RIGHT, -22, 20);

  wx_icon = wx_label(wx_root, &font_icons_80, kAmber, 16, 40);
  wx_temp = wx_label(wx_root, &font_inter_light_40, kText, 112, 50);
  lv_label_set_recolor(wx_temp, true);
  wx_cond = wx_label(wx_root, &font_inter_16, kTextDim, 114, 100);
  lv_obj_set_width(wx_cond, 344);
  lv_label_set_long_mode(wx_cond, LV_LABEL_LONG_DOT);
  wx_empty = wx_label(wx_root, &font_inter_20, kTextDim, 114, 64);

  // Info chips: rain, wind, sunrise, sunset.
  static const char* kChipIcon[4] = {ICON_DROP, ICON_AIR, ICON_SUNRISE, ICON_SUNSET};
  for (int i = 0; i < 4; ++i) {
    lv_obj_t* chip = lv_obj_create(wx_root);
    lv_obj_set_pos(chip, 20 + i * 112, 136);
    lv_obj_set_size(chip, 104, 52);
    style_glass(chip, 14);
    lv_obj_clear_flag(chip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t* ic = wx_label(chip, &font_icons_16, kAccent, 10, 6);
    lv_label_set_text(ic, kChipIcon[i]);
    wx_info[i] = wx_label(chip, &font_inter_sb16, kText, 10, 25);
  }

  lv_obj_t* tl = lv_obj_create(wx_root);
  lv_obj_set_pos(tl, 20, 200);
  lv_obj_set_size(tl, kScreen - 40, 232);
  style_glass(tl, 22);
  lv_obj_clear_flag(tl, LV_OBJ_FLAG_CLICKABLE);

  wx_curve = lv_line_create(tl);
  lv_obj_set_pos(wx_curve, 0, 0);
  lv_obj_set_style_line_width(wx_curve, 3, 0);
  lv_obj_set_style_line_color(wx_curve, lv_color_hex(kAccent), 0);
  lv_obj_set_style_line_rounded(wx_curve, true, 0);

  for (int i = 0; i < kWxSlots; ++i) {
    int cx = i * kColW;
    wx_hour[i] = wx_label(tl, &font_inter_14, kTextDim, cx, 8);
    lv_obj_set_width(wx_hour[i], kColW);
    lv_obj_set_style_text_align(wx_hour[i], LV_TEXT_ALIGN_CENTER, 0);
    char h[4];
    snprintf(h, sizeof(h), "%02d", i * 3);
    lv_label_set_text(wx_hour[i], h);
    wx_sicon[i] = wx_label(tl, &font_icons_24, kAmber, cx, 30);
    lv_obj_set_width(wx_sicon[i], kColW);
    lv_obj_set_style_text_align(wx_sicon[i], LV_TEXT_ALIGN_CENTER, 0);
    wx_dot[i] = make_bar(tl, cx + kColW / 2 - 4, kCurveTop, 8, 8, kAccent, LV_OPA_COVER);
    wx_stemp[i] = wx_label(tl, &font_inter_sb16, kText, cx, 120);
    lv_obj_set_width(wx_stemp[i], kColW);
    lv_obj_set_style_text_align(wx_stemp[i], LV_TEXT_ALIGN_CENTER, 0);
    wx_smm[i] = wx_label(tl, &font_inter_14, kAccent, cx, 148);
    lv_obj_set_width(wx_smm[i], kColW);
    lv_obj_set_style_text_align(wx_smm[i], LV_TEXT_ALIGN_CENTER, 0);
    wx_sprob[i] = wx_label(tl, &font_inter_14, kTextDim, cx, 166);
    lv_obj_set_width(wx_sprob[i], kColW);
    lv_obj_set_style_text_align(wx_sprob[i], LV_TEXT_ALIGN_CENTER, 0);
    wx_swind[i] = wx_label(tl, &font_inter_14, kTextDim, cx, 184);
    lv_obj_set_width(wx_swind[i], kColW);
    lv_obj_set_style_text_align(wx_swind[i], LV_TEXT_ALIGN_CENTER, 0);
  }

  // Row units for the three bottom rows.
  lv_obj_t* legend = make_label(tl, &font_inter_14, lv_color_hex(kTextDim));
  lv_label_set_text(legend, "Regen mm  \xC2\xB7  Wahrscheinlichkeit %  \xC2\xB7  Wind km/h");
  lv_obj_align(legend, LV_ALIGN_BOTTOM_MID, 0, -8);
  lv_obj_set_style_text_opa(legend, LV_OPA_70, 0);

  wx_prev_hint = wx_handle(wx_root, 5);
  wx_next_hint = wx_handle(wx_root, 470);
  lv_obj_t* up = make_label(wx_root, &font_icons_24, lv_color_hex(kTextDim));
  lv_label_set_text(up, ICON_UP);
  lv_obj_align(up, LV_ALIGN_BOTTOM_MID, 0, -14);
  make_home_bar(wx_root);
  hide(wx_root);
}

void refresh_weather_day() {
  if (wx_root == nullptr) {
    return;
  }
  if (wx_count <= 0 || wx_week == nullptr) {
    lv_label_set_text(wx_title, "Wetter");
    lv_label_set_text(wx_page, "");
    lv_label_set_text(wx_empty, "Noch keine Vorhersage");
    show(wx_empty);
    hide(wx_prev_hint);
    hide(wx_next_hint);
    return;
  }
  hide(wx_empty);
  if (wx_day >= wx_count) wx_day = wx_count - 1;
  const WeatherDetailDay& d = wx_week[wx_day];
  int y = atoi(d.date), m = atoi(d.date + 5), dd = atoi(d.date + 8);
  char buf[128];
  const char* rel = wx_day == 0 ? "Heute \xC2\xB7 " : wx_day == 1 ? "Morgen \xC2\xB7 " : "";
  bool date_ok = m >= 1 && m <= 12;
  snprintf(buf, sizeof(buf), "%s%s, %d. %s \xC2\xB7 %s", rel, date_ok ? kWeekday[weekday_of(y, m, dd)] : "", dd,
           date_ok ? kMonth[m - 1] : "", cur_place);
  lv_label_set_text(wx_title, buf);
  snprintf(buf, sizeof(buf), "%d / %d", wx_day + 1, wx_count);
  lv_label_set_text(wx_page, buf);
  lv_obj_align(wx_page, LV_ALIGN_TOP_RIGHT, -22, 20);
  day_icon(wx_icon, d.kind);
  snprintf(buf, sizeof(buf), "%d\xC2\xB0 #8A94A8 / %d\xC2\xB0#", round_c(d.t_max), round_c(d.t_min));
  lv_label_set_text(wx_temp, buf);
  snprintf(buf, sizeof(buf), "%s \xC2\xB7 Regen %d %%", day_kind_name(static_cast<DayKind>(d.kind)), d.prob_max);
  lv_label_set_text(wx_cond, buf);
  snprintf(buf, sizeof(buf), "%.1f mm", static_cast<double>(d.precip));
  lv_label_set_text(wx_info[0], buf);
  snprintf(buf, sizeof(buf), "%d km/h", round_c(d.wind_max));
  lv_label_set_text(wx_info[1], buf);
  lv_label_set_text(wx_info[2], d.sunrise);
  lv_label_set_text(wx_info[3], d.sunset);

  float lo = d.slot[0].temp, hi = d.slot[0].temp;
  for (int i = 0; i < kWxSlots; ++i) {
    if (d.slot[i].temp < lo) lo = d.slot[i].temp;
    if (d.slot[i].temp > hi) hi = d.slot[i].temp;
  }
  float span = hi - lo < 1.f ? 1.f : hi - lo;
  for (int i = 0; i < kWxSlots; ++i) {
    const WeatherSlot& sl = d.slot[i];
    weather_icon(wx_sicon[i], sl.code);
    snprintf(buf, sizeof(buf), "%d\xC2\xB0", round_c(sl.temp));
    lv_label_set_text(wx_stemp[i], buf);
    wx_curve_pts[i].x = i * kColW + kColW / 2;
    wx_curve_pts[i].y = kCurveTop + kCurveH - static_cast<int>((sl.temp - lo) / span * kCurveH);
    lv_obj_set_pos(wx_dot[i], wx_curve_pts[i].x - 4, wx_curve_pts[i].y - 4);
    snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(sl.precip));
    lv_label_set_text(wx_smm[i], buf);
    snprintf(buf, sizeof(buf), "%d%%", sl.prob);
    lv_label_set_text(wx_sprob[i], buf);
    snprintf(buf, sizeof(buf), "%d", round_c(sl.wind));
    lv_label_set_text(wx_swind[i], buf);
  }
  lv_line_set_points(wx_curve, wx_curve_pts, kWxSlots);
  if (wx_day > 0) show(wx_prev_hint); else hide(wx_prev_hint);
  if (wx_day + 1 < wx_count) show(wx_next_hint); else hide(wx_next_hint);
}

// --- Settings -------------------------------------------------------------
// Einstellungen: WLAN (scan, pick, password, connect) and Wetter-Ort
// (Open-Meteo geocoding). The network work runs in the net task; the UI only
// queues one SettingsRequest (ui_take_settings) and gets ui_*_done back.

lv_obj_t* make_screen_root(lv_obj_t* screen, const char* title) {
  lv_obj_t* root = lv_obj_create(screen);
  lv_obj_set_pos(root, 0, 0);
  lv_obj_set_size(root, kScreen, kScreen);
  style_plain(root);
  lv_obj_t* t = make_label(root, &font_inter_20, lv_color_hex(kText));
  lv_obj_set_pos(t, 24, 20);
  lv_label_set_text(t, title);
  make_home_bar(root);
  hide(root);
  return root;
}

lv_obj_t* make_list(lv_obj_t* parent, int y, int h) {
  lv_obj_t* list = lv_obj_create(parent);
  lv_obj_set_pos(list, kMargin, y);
  lv_obj_set_size(list, kScreen - 2 * kMargin, h);
  style_plain(list);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(list, 8, 0);
  lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
  return list;
}

lv_obj_t* make_row(lv_obj_t* parent, int h, const char* icon, uint32_t icon_color, lv_event_cb_t cb, int user) {
  lv_obj_t* row = lv_btn_create(parent);
  lv_obj_set_size(row, kScreen - 2 * kMargin, h);
  style_glass(row, 18);
  lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<intptr_t>(user)));
  watch(row);
  if (icon != nullptr) {
    lv_obj_t* ic = make_label(row, &font_icons_24, lv_color_hex(icon_color));
    lv_label_set_text(ic, icon);
    lv_obj_align(ic, LV_ALIGN_LEFT_MID, 16, 0);
  }
  return row;
}

void show_keyboard(lv_obj_t* ta) {
  lv_keyboard_set_textarea(kb, ta);
  lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
  show(kb);
  lv_obj_move_foreground(kb);
}

void queue_settings(const SettingsRequest& r) {
  settings_req = r;
  settings_pending = true;
}

const char* timeout_text(uint16_t s) {
  switch (s) {
    case 0: return "Aus";
    case 30: return "30 s";
    case 60: return "1 min";
    case 120: return "2 min";
    case 300: return "5 min";
    case 600: return "10 min";
    default: return "?";
  }
}

void refresh_display_settings() {
  char buf[64];
  static const char* kSaverName[3] = {"Schwarz", "Digitaluhr", "Analoguhr"};
  if (disp.timeout_s == 0) {
    snprintf(buf, sizeof(buf), "Bleibt an");
  } else if (disp.saver == kSaverBlack) {
    snprintf(buf, sizeof(buf), "Nach %s: Schwarz, Hinweis %s", timeout_text(disp.timeout_s), disp.hint ? "an" : "aus");
  } else {
    snprintf(buf, sizeof(buf), "Nach %s: %s", timeout_text(disp.timeout_s), kSaverName[disp.saver]);
  }
  for (int i = 0; i < 3; ++i) {
    if (disp_saver_btn[i] == nullptr) continue;
    if (i == disp.saver) {
      lv_obj_add_state(disp_saver_btn[i], LV_STATE_CHECKED);
    } else {
      lv_obj_clear_state(disp_saver_btn[i], LV_STATE_CHECKED);
    }
  }
  if (set_disp_val != nullptr) lv_label_set_text(set_disp_val, buf);
  for (int i = 0; i < kTimeoutChoiceN; ++i) {
    if (disp_to_btn[i] == nullptr) continue;
    if (kTimeoutChoices[i] == disp.timeout_s) {
      lv_obj_add_state(disp_to_btn[i], LV_STATE_CHECKED);
    } else {
      lv_obj_clear_state(disp_to_btn[i], LV_STATE_CHECKED);
    }
  }
  if (disp_hint_sw != nullptr) {
    if (disp.hint) {
      lv_obj_add_state(disp_hint_sw, LV_STATE_CHECKED);
    } else {
      lv_obj_clear_state(disp_hint_sw, LV_STATE_CHECKED);
    }
  }
}

void open_display() {
  nav = Nav::Display;
  hide_settings();
  refresh_display_settings();
  show(disp_root);
}

void on_timeout_btn(lv_event_t* e) {
  if (gesture_consumed) return;
  int i = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  disp.timeout_s = kTimeoutChoices[i];
  scr_timeout_s = disp.timeout_s;
  last_activity = now_ms();
  bool ok = settings_save_display(disp);
  console_printf("Screen timeout %u s saved=%d\n", static_cast<unsigned>(disp.timeout_s), ok ? 1 : 0);
  refresh_display_settings();
}

void on_saver_btn(lv_event_t* e) {
  if (gesture_consumed) return;
  int i = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  disp.saver = static_cast<uint8_t>(i);
  bool ok = settings_save_display(disp);
  console_printf("Screen saver %d saved=%d\n", i, ok ? 1 : 0);
  refresh_display_settings();
}

void on_hint_row(lv_event_t*) {
  if (gesture_consumed) return;
  disp.hint = !disp.hint;
  bool ok = settings_save_display(disp);
  console_printf("Screen hint %d saved=%d\n", disp.hint ? 1 : 0, ok ? 1 : 0);
  refresh_display_settings();
}

// --- Screen timeout and screensavers ---
// While off, sleep_layer (a screen of its own) is the active screen, so LVGL
// only renders it: black, the dim "wake me up", or one of the clocks.
void move_hint() {
  int x = 16 + static_cast<int>(esp_random() % (kScreen - 140));
  int y = 16 + static_cast<int>(esp_random() % (kScreen - 50));
  lv_obj_set_pos(sleep_hint, x, y);
  hint_moved_ms = now_ms();
}

bool local_now(struct tm* out, int* ms) {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  time_t t = tv.tv_sec;
  localtime_r(&t, out);
  if (ms != nullptr) *ms = static_cast<int>(tv.tv_usec / 1000);
  return out->tm_year + 1900 >= 2024;  // false until SNTP has set the clock
}

// Swiss railway clock geometry (fractions of the dial radius R).
constexpr float kDialR = 200.f;
lv_point_t polar(float cx, float cy, float deg, float r) {
  float a = deg * static_cast<float>(M_PI) / 180.f;
  lv_point_t p;
  p.x = static_cast<lv_coord_t>(lroundf(cx + r * sinf(a)));
  p.y = static_cast<lv_coord_t>(lroundf(cy - r * cosf(a)));
  return p;
}

// Tapered bar from -back to +len along deg (a hand), drawn as a polygon.
void draw_hand(lv_draw_ctx_t* ctx, float cx, float cy, float deg, float back, float len, float w_back, float w_tip,
               lv_color_t color) {
  float a = deg * static_cast<float>(M_PI) / 180.f;
  float dx = sinf(a), dy = -cosf(a);  // along the hand
  float nx = -dy, ny = dx;            // across
  float bx = cx - dx * back, by = cy - dy * back;
  float tx = cx + dx * len, ty = cy + dy * len;
  lv_point_t pts[4] = {
      {static_cast<lv_coord_t>(lroundf(bx + nx * w_back / 2)), static_cast<lv_coord_t>(lroundf(by + ny * w_back / 2))},
      {static_cast<lv_coord_t>(lroundf(tx + nx * w_tip / 2)), static_cast<lv_coord_t>(lroundf(ty + ny * w_tip / 2))},
      {static_cast<lv_coord_t>(lroundf(tx - nx * w_tip / 2)), static_cast<lv_coord_t>(lroundf(ty - ny * w_tip / 2))},
      {static_cast<lv_coord_t>(lroundf(bx - nx * w_back / 2)), static_cast<lv_coord_t>(lroundf(by - ny * w_back / 2))},
  };
  lv_draw_rect_dsc_t dsc;
  lv_draw_rect_dsc_init(&dsc);
  dsc.bg_color = color;
  dsc.bg_opa = LV_OPA_COVER;
  lv_draw_polygon(ctx, &dsc, pts, 4);
}

void draw_disc(lv_draw_ctx_t* ctx, float x, float y, float r, lv_color_t color) {
  lv_draw_rect_dsc_t dsc;
  lv_draw_rect_dsc_init(&dsc);
  dsc.radius = LV_RADIUS_CIRCLE;
  dsc.bg_color = color;
  dsc.bg_opa = LV_OPA_COVER;
  lv_area_t a = {static_cast<lv_coord_t>(lroundf(x - r)), static_cast<lv_coord_t>(lroundf(y - r)),
                 static_cast<lv_coord_t>(lroundf(x + r)), static_cast<lv_coord_t>(lroundf(y + r))};
  lv_draw_rect(ctx, &dsc, &a);
}

// SBB clock second hand: sweeps the dial in 58.5 s, rests at 12 until the
// minute hand jumps on the full minute.
float second_angle(const struct tm& t, int ms) {
  float s = static_cast<float>(t.tm_sec) + static_cast<float>(ms) / 1000.f;
  return s >= 58.5f ? 0.f : s * (360.f / 58.5f);
}

// Bounding box of the second hand (red bar plus the disc at its tip).
lv_area_t second_area(lv_obj_t* obj, float deg) {
  lv_area_t c;
  lv_obj_get_coords(obj, &c);
  float cx = (c.x1 + c.x2) / 2.f, cy = (c.y1 + c.y2) / 2.f;
  lv_point_t p1 = polar(cx, cy, deg + 180.f, kDialR * 0.31f);
  lv_point_t p2 = polar(cx, cy, deg, kDialR * 0.645f);
  int m = static_cast<int>(kDialR * 0.105f) + 4;
  lv_area_t a;
  a.x1 = LV_MIN(p1.x, p2.x) - m;
  a.y1 = LV_MIN(p1.y, p2.y) - m;
  a.x2 = LV_MAX(p1.x, p2.x) + m;
  a.y2 = LV_MAX(p1.y, p2.y) + m;
  return a;
}

void analog_draw(lv_event_t* e) {
  lv_obj_t* obj = lv_event_get_target(e);
  lv_draw_ctx_t* ctx = lv_event_get_draw_ctx(e);
  lv_area_t c;
  lv_obj_get_coords(obj, &c);
  float cx = (c.x1 + c.x2) / 2.f, cy = (c.y1 + c.y2) / 2.f;
  const float R = kDialR;
  lv_color_t black = lv_color_hex(0x111111);
  lv_color_t red = lv_color_hex(0xEB0000);
  draw_disc(ctx, cx, cy, R + 12.f, lv_color_hex(0x3A3F48));  // case
  draw_disc(ctx, cx, cy, R, lv_color_hex(0xF4F4F2));         // white face
  lv_draw_line_dsc_t ld;
  lv_draw_line_dsc_init(&ld);
  ld.color = black;
  ld.opa = LV_OPA_COVER;
  for (int i = 0; i < 60; ++i) {
    bool hour = i % 5 == 0;
    ld.width = static_cast<lv_coord_t>(hour ? R * 0.075f : R * 0.028f);
    lv_point_t p1 = polar(cx, cy, i * 6.f, hour ? R * 0.70f : R * 0.86f);
    lv_point_t p2 = polar(cx, cy, i * 6.f, R * 0.95f);
    lv_draw_line(ctx, &ld, &p1, &p2);
  }
  struct tm t;
  int ms = 0;
  local_now(&t, &ms);
  float hour_deg = static_cast<float>(t.tm_hour % 12) * 30.f + static_cast<float>(t.tm_min) * 0.5f;
  float min_deg = static_cast<float>(t.tm_min) * 6.f;  // jumps on the full minute
  draw_hand(ctx, cx, cy, hour_deg, R * 0.24f, R * 0.64f, R * 0.105f, R * 0.085f, black);
  draw_hand(ctx, cx, cy, min_deg, R * 0.24f, R * 0.93f, R * 0.085f, R * 0.06f, black);
  float sd = sec_angle < 0.f ? second_angle(t, ms) : sec_angle;
  ld.color = red;
  ld.width = static_cast<lv_coord_t>(R * 0.024f);
  lv_point_t s1 = polar(cx, cy, sd + 180.f, R * 0.31f);
  lv_point_t s2 = polar(cx, cy, sd, R * 0.645f);
  lv_draw_line(ctx, &ld, &s1, &s2);
  lv_point_t tip = polar(cx, cy, sd, R * 0.645f);
  draw_disc(ctx, tip.x, tip.y, R * 0.105f, red);
  draw_disc(ctx, cx, cy, R * 0.035f, red);
}

void saver_update(bool force) {
  struct tm t;
  int ms = 0;
  bool valid = local_now(&t, &ms);
  int minute = t.tm_hour * 60 + t.tm_min;
  if (disp.saver == kSaverDigital) {
    if (!force && minute == saver_last_min) return;
    saver_last_min = minute;
    char buf[48];
    if (valid) {
      snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
      lv_label_set_text(dig_time, buf);
      snprintf(buf, sizeof(buf), "%s, %d. %s", kWeekday[t.tm_wday], t.tm_mday, kMonth[t.tm_mon]);
      lv_label_set_text(dig_date, buf);
    } else {
      lv_label_set_text(dig_time, "--:--");
      lv_label_set_text(dig_date, "Uhrzeit wird synchronisiert");
    }
    // Small drift every minute against burn-in.
    int k = minute % 9;
    lv_obj_align(saver_digital, LV_ALIGN_CENTER, (k % 3 - 1) * 14, (k / 3 - 1) * 14);
    return;
  }
  if (disp.saver != kSaverAnalog) return;
  if (force || minute != saver_last_min) {
    saver_last_min = minute;
    sec_angle = second_angle(t, ms);
    sec_area = second_area(saver_analog, sec_angle);
    lv_obj_invalidate(saver_analog);  // hour/minute hands moved
    return;
  }
  float deg = second_angle(t, ms);
  if (fabsf(deg - sec_angle) < 0.3f) return;  // resting at 12, or no visible step
  lv_obj_invalidate_area(saver_analog, &sec_area);  // erase the old hand
  sec_angle = deg;
  sec_area = second_area(saver_analog, deg);
  lv_obj_invalidate_area(saver_analog, &sec_area);  // draw the new one
}

void saver_tick(lv_timer_t*) { saver_update(false); }

void screen_off() {
  if (scr_off) return;
  scr_off = true;
  bool clock = disp.saver != kSaverBlack;
  if (clock) {
    hide(sleep_hint);
  } else if (disp.hint) {
    show(sleep_hint);
    move_hint();
  } else {
    hide(sleep_hint);
  }
  if (disp.saver == kSaverDigital) show(saver_digital); else hide(saver_digital);
  if (disp.saver == kSaverAnalog) show(saver_analog); else hide(saver_analog);
  lv_scr_load(sleep_layer);
  if (clock) {
    saver_last_min = -1;
    sec_angle = -1.f;
    saver_update(true);
    // Digital: once a second is enough; analog: ~12 fps sweep (dirty rects only).
    lv_timer_set_period(saver_timer, disp.saver == kSaverAnalog ? 80 : 1000);
    lv_timer_resume(saver_timer);
  }
  panel_backlight(clock ? kClockBacklight : disp.hint ? kHintBacklight : 0, 600);
  rem_tick(true);
  console_printf("Screen off (saver %d hint %d)\n", disp.saver, disp.hint ? 1 : 0);
}

void hide_sleep_layer(void*) {
  lv_scr_load(main_screen);
  rem_tick(true);
}

void screen_on() {
  scr_off = false;
  last_activity = now_ms();
  lv_timer_pause(saver_timer);
  panel_backlight(100, 150);
  lv_async_call(hide_sleep_layer, nullptr);  // not from inside the input read
  console_puts("Screen on");
}

void power_tick(lv_timer_t*) {
  uint32_t now = now_ms();
  if (scr_off) {
    if (disp.saver == kSaverBlack && disp.hint && now - hint_moved_ms >= kHintMoveMs) move_hint();
    return;
  }
  if (scr_timeout_s != 0 && !wake_swallow && now - last_activity >= scr_timeout_s * 1000u) screen_off();
}

void build_sleep_layer() {
  sleep_layer = lv_obj_create(nullptr);  // a screen of its own
  lv_obj_set_style_bg_color(sleep_layer, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(sleep_layer, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(sleep_layer, 0, 0);
  lv_obj_set_style_radius(sleep_layer, 0, 0);
  lv_obj_set_style_pad_all(sleep_layer, 0, 0);
  lv_obj_clear_flag(sleep_layer, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(sleep_layer, LV_OBJ_FLAG_CLICKABLE);
  sleep_hint = make_label(sleep_layer, &font_inter_14, lv_color_hex(0x9A9A9A));
  lv_label_set_text(sleep_hint, "wake me up");

  saver_digital = lv_obj_create(sleep_layer);
  style_plain(saver_digital);
  lv_obj_set_size(saver_digital, 440, 240);
  dig_time = make_label(saver_digital, &font_clock_150, lv_color_hex(kText));
  lv_label_set_text(dig_time, "--:--");
  lv_obj_align(dig_time, LV_ALIGN_TOP_MID, 0, 10);
  dig_date = make_label(saver_digital, &font_inter_20, lv_color_hex(kTextDim));
  lv_obj_align(dig_date, LV_ALIGN_BOTTOM_MID, 0, -16);
  lv_obj_set_style_text_align(dig_date, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(dig_date, 440);
  hide(saver_digital);

  saver_analog = lv_obj_create(sleep_layer);
  style_plain(saver_analog);
  lv_obj_set_style_bg_opa(saver_analog, LV_OPA_TRANSP, 0);
  lv_obj_set_size(saver_analog, kClockD, kClockD);
  lv_obj_center(saver_analog);
  lv_obj_add_event_cb(saver_analog, analog_draw, LV_EVENT_DRAW_MAIN, nullptr);
  hide(saver_analog);

  saver_timer = lv_timer_create(saver_tick, 1000, nullptr);
  lv_timer_pause(saver_timer);
  disp = settings_load_display();
  scr_timeout_s = disp.timeout_s;
  last_activity = now_ms();
  lv_timer_create(power_tick, 250, nullptr);
}

// --- Uhrzeit (NTP server and time zone) ---
void refresh_time_settings() {
  char buf[96];
  snprintf(buf, sizeof(buf), "%s \xC2\xB7 %s", tset.server, tset.tz_name);
  if (set_time_val != nullptr) lv_label_set_text(set_time_val, buf);
  for (int i = 0; i < 4; ++i) {
    if (time_srv_btn[i] == nullptr) continue;
    if (strcmp(tset.server, kNtpServers[i]) == 0) lv_obj_add_state(time_srv_btn[i], LV_STATE_CHECKED);
    else lv_obj_clear_state(time_srv_btn[i], LV_STATE_CHECKED);
    if (strcmp(tset.tz_name, kTzChoices[i].name) == 0) lv_obj_add_state(time_tz_btn[i], LV_STATE_CHECKED);
    else lv_obj_clear_state(time_tz_btn[i], LV_STATE_CHECKED);
  }
  if (time_status == nullptr) return;
  struct tm t;
  if (time_saving) {
    snprintf(buf, sizeof(buf), "Speichere ...");
  } else if (local_now(&t, nullptr)) {
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d \xC2\xB7 %s", t.tm_hour, t.tm_min, t.tm_sec,
             clock_synced() ? "synchronisiert" : "noch nicht synchronisiert");
  } else {
    snprintf(buf, sizeof(buf), "Noch keine Zeit vom Server");
  }
  lv_label_set_text(time_status, buf);
}

void save_time() {
  if (settings_pending) return;
  SettingsRequest r = {};
  r.kind = SettingsRequest::SetTime;
  r.time = tset;
  time_saving = true;
  queue_settings(r);
  clock_apply_tz(tset.tz);
  refresh_time_settings();
}

void open_time() {
  nav = Nav::Time;
  hide_settings();
  bool preset = false;
  for (int i = 0; i < 4; ++i) preset = preset || strcmp(tset.server, kNtpServers[i]) == 0;
  lv_textarea_set_text(time_ta, preset ? "" : tset.server);
  refresh_time_settings();
  show(time_root);
}

void on_srv_btn(lv_event_t* e) {
  if (gesture_consumed) return;
  int i = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  snprintf(tset.server, sizeof(tset.server), "%s", kNtpServers[i]);
  lv_textarea_set_text(time_ta, "");
  console_printf("Time server %s\n", tset.server);
  save_time();
}

void on_tz_btn(lv_event_t* e) {
  if (gesture_consumed) return;
  int i = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  snprintf(tset.tz_name, sizeof(tset.tz_name), "%s", kTzChoices[i].name);
  snprintf(tset.tz, sizeof(tset.tz), "%s", kTzChoices[i].posix);
  console_printf("Time zone %s\n", tset.tz_name);
  save_time();
}

void on_time_ready(lv_event_t*) {
  const char* q = lv_textarea_get_text(time_ta);
  if (q[0] == '\0') return;
  snprintf(tset.server, sizeof(tset.server), "%s", q);
  lv_keyboard_set_textarea(kb, nullptr);
  hide(kb);
  console_printf("Time server %s (custom)\n", tset.server);
  save_time();
}

// 1 s: home header clock, time settings status.
void clock_tick(lv_timer_t*) {
  static int last_min = -2;
  struct tm t;
  bool valid = local_now(&t, nullptr);
  int minute = valid ? t.tm_yday * 1440 + t.tm_hour * 60 + t.tm_min : -1;
  if (minute != last_min && home_time != nullptr) {
    last_min = minute;
    char buf[32];
    if (valid) {
      snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
      lv_label_set_text(home_time, buf);
      snprintf(buf, sizeof(buf), "%s, %d. %s", kWeekdayShort[t.tm_wday], t.tm_mday, kMonthShort[t.tm_mon]);
      lv_label_set_text(home_date, buf);
    } else {
      lv_label_set_text(home_time, "--:--");
      lv_label_set_text(home_date, "");
    }
  }
  if (nav == Nav::Time && !scr_off) refresh_time_settings();
  cam_tick();
  info_tick();
}

void open_settings() {
  if (message_open) return;
  nav = Nav::Settings;
  hide(cam_root);
  hide(home_root);
  hide(cat_root);
  hide(wx_root);
  hide_settings();
  info_hide_views();
  trset_refresh();
  calset_refresh();
  maint_refresh();
  lv_label_set_text(set_wifi_val, cur_ssid[0] != '\0' ? cur_ssid : "nicht verbunden");
  lv_label_set_text(set_loc_val, cur_place);
  refresh_display_settings();
  refresh_time_settings();
  camset_value();
  show(set_root);
}

void settings_back() { open_settings(); }

void on_wifi_row(lv_event_t* e);
void on_loc_row(lv_event_t* e);

void fill_wifi_list() {
  unreg_prefix("wifi:");
  lv_obj_clean(wl_list);
  for (int i = 0; i < ui_ap_n; ++i) {
    const WifiAp& ap = ui_aps[i];
    const char* bars = ap.rssi >= -55 ? ICON_WIFI3 : ap.rssi >= -67 ? ICON_WIFI2 : ap.rssi >= -78 ? ICON_WIFI1 : ICON_WIFI0;
    lv_obj_t* row = make_row(wl_list, 56, bars, kCyan, on_wifi_row, i);
    lv_obj_t* name = make_label(row, &font_inter_sb16, lv_color_hex(kText));
    lv_obj_set_width(name, 260);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_label_set_text(name, ap.ssid);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 54, 0);
    char info[24];
    snprintf(info, sizeof(info), "%d dBm", ap.rssi);
    lv_obj_t* dbm = make_label(row, &font_inter_14, lv_color_hex(kTextDim));
    lv_label_set_text(dbm, info);
    lv_obj_align(dbm, LV_ALIGN_RIGHT_MID, (ap.secure || strcmp(ap.ssid, cur_ssid) == 0) ? -46 : -16, 0);
    if (ap.secure || strcmp(ap.ssid, cur_ssid) == 0) {
      lv_obj_t* ic = make_label(row, &font_icons_24, lv_color_hex(strcmp(ap.ssid, cur_ssid) == 0 ? kGreen : kTextDim));
      lv_label_set_text(ic, strcmp(ap.ssid, cur_ssid) == 0 ? ICON_CHECK : ICON_LOCK);
      lv_obj_align(ic, LV_ALIGN_RIGHT_MID, -14, 0);
    }
    char key[16];
    snprintf(key, sizeof(key), "wifi:%d", i);
    reg(key, row);
  }
}

void open_wifi_list() {
  nav = Nav::WifiList;
  hide_settings();
  show(wl_root);
  ui_ap_n = 0;
  unreg_prefix("wifi:");
  lv_obj_clean(wl_list);
  wl_scanning = true;
  lv_label_set_text(wl_status, "Suche Netzwerke ...");
  SettingsRequest r = {};
  r.kind = SettingsRequest::Scan;
  queue_settings(r);
}

void on_wifi_row(lv_event_t* e) {
  if (gesture_consumed) return;
  int i = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  if (i < 0 || i >= ui_ap_n) return;
  console_printf("Activate wifi %d %s\n", i, ui_aps[i].ssid);
  nav = Nav::WifiPass;
  snprintf(wp_ssid, sizeof(wp_ssid), "%s", ui_aps[i].ssid);
  hide_settings();
  char t[64];
  snprintf(t, sizeof(t), "WLAN: %s", wp_ssid);
  lv_label_set_text(wp_title, t);
  lv_textarea_set_text(wp_ta, "");
  lv_label_set_text(wp_status, ui_aps[i].secure ? "Passwort eingeben, dann OK" : "Offenes Netz: OK zum Verbinden");
  show(wp_root);
  show_keyboard(wp_ta);
}

void on_wifi_ready(lv_event_t*) {
  if (wp_connecting || settings_pending) return;
  SettingsRequest r = {};
  r.kind = SettingsRequest::Connect;
  snprintf(r.creds.ssid, sizeof(r.creds.ssid), "%s", wp_ssid);
  snprintf(r.creds.pass, sizeof(r.creds.pass), "%s", lv_textarea_get_text(wp_ta));
  wp_connecting = true;
  lv_label_set_text(wp_status, "Verbinde ...");
  queue_settings(r);
}

void fill_loc_list() {
  unreg_prefix("loc:");
  lv_obj_clean(loc_list);
  for (int i = 0; i < ui_place_n; ++i) {
    lv_obj_t* row = make_row(loc_list, 64, ICON_PLACE, kOrange, on_loc_row, i);
    lv_obj_t* name = make_label(row, &font_inter_20, lv_color_hex(kText));
    lv_label_set_text(name, ui_places[i].name);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 54, 10);
    lv_obj_t* reg_l = make_label(row, &font_inter_14, lv_color_hex(kTextDim));
    lv_obj_set_width(reg_l, 340);
    lv_label_set_long_mode(reg_l, LV_LABEL_LONG_DOT);
    lv_label_set_text(reg_l, ui_places[i].region);
    lv_obj_align(reg_l, LV_ALIGN_BOTTOM_LEFT, 54, -10);
    char key[16];
    snprintf(key, sizeof(key), "loc:%d", i);
    reg(key, row);
  }
}

void open_location() {
  nav = Nav::Location;
  hide_settings();
  ui_place_n = 0;
  unreg_prefix("loc:");
  lv_obj_clean(loc_list);
  lv_textarea_set_text(loc_ta, "");
  lv_label_set_text(loc_status, "Ortsname eingeben, dann OK");
  show(loc_root);
  show_keyboard(loc_ta);
}

void on_loc_ready(lv_event_t*) {
  const char* q = lv_textarea_get_text(loc_ta);
  if (q[0] == '\0' || settings_pending || loc_searching) return;
  SettingsRequest r = {};
  r.kind = SettingsRequest::Geocode;
  snprintf(r.query, sizeof(r.query), "%s", q);
  loc_searching = true;
  lv_label_set_text(loc_status, "Suche ...");
  lv_keyboard_set_textarea(kb, nullptr);
  hide(kb);
  queue_settings(r);
}

void on_loc_row(lv_event_t* e) {
  if (gesture_consumed || settings_pending) return;
  int i = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  if (i < 0 || i >= ui_place_n) return;
  console_printf("Activate location %d %s\n", i, ui_places[i].name);
  SettingsRequest r = {};
  r.kind = SettingsRequest::SetLocation;
  snprintf(r.location.name, sizeof(r.location.name), "%s", ui_places[i].name);
  r.location.lat = ui_places[i].lat;
  r.location.lon = ui_places[i].lon;
  lv_label_set_text(loc_status, "Speichere ...");
  queue_settings(r);
}

void on_kb_event(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t* ta = lv_keyboard_get_textarea(kb);
  if (code == LV_EVENT_READY) {
    if (ta == wp_ta) on_wifi_ready(e);
    if (ta == loc_ta) on_loc_ready(e);
    if (ta == time_ta) on_time_ready(e);
    cam_kb_ready(ta);
    st_kb_ready(ta);
    txt_kb_ready(ta);
  } else if (code == LV_EVENT_CANCEL) {
    lv_keyboard_set_textarea(kb, nullptr);
    hide(kb);
  }
}

void on_ta_click(lv_event_t* e) {
  if (gesture_consumed) return;
  show_keyboard(lv_event_get_target(e));
}

void on_settings_row(lv_event_t* e) {
  if (gesture_consumed) return;
  int which = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  console_printf("Activate settings row %d\n", which);
  if (which == 0) open_wifi_list();
  if (which == 1) open_location();
  if (which == 2) open_display();
  if (which == 3) open_time();
  if (which == 4) open_camset();
  if (which == 5) open_trset();
  if (which == 6) open_calset();
  if (which == 7) open_remset();
  if (which == 8) open_maint();
}

void on_msg_wifi(lv_event_t*) {
  if (gesture_consumed) return;
  message_open = false;
  hide(message);
  hide(msg_status);
  open_settings();
  open_wifi_list();
}

lv_obj_t* make_ta(lv_obj_t* parent, int y, const char* placeholder) {
  lv_obj_t* ta = lv_textarea_create(parent);
  lv_obj_set_pos(ta, kMargin, y);
  lv_obj_set_size(ta, kScreen - 2 * kMargin, 52);
  lv_textarea_set_one_line(ta, true);
  lv_textarea_set_placeholder_text(ta, placeholder);
  lv_obj_set_style_text_font(ta, &font_inter_20, 0);
  style_glass(ta, 14);
  lv_obj_set_style_pad_all(ta, 12, 0);
  lv_obj_set_style_text_color(ta, lv_color_hex(kText), 0);
  lv_obj_set_style_border_color(ta, lv_color_hex(kAccent), LV_STATE_FOCUSED);
  lv_obj_add_event_cb(ta, on_ta_click, LV_EVENT_CLICKED, nullptr);
  return ta;
}

lv_obj_t* make_choice(lv_obj_t* parent, int x, int y, int w, int h, const char* text, lv_event_cb_t cb, int user,
                      const char* key) {
  lv_obj_t* b = lv_btn_create(parent);
  lv_obj_set_size(b, w, h);
  lv_obj_set_pos(b, x, y);
  style_glass(b, 16);
  style_checked(b);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<intptr_t>(user)));
  watch(b);
  reg(key, b);
  lv_obj_t* t = make_label(b, &font_inter_sb16, lv_color_hex(kText));
  lv_label_set_text(t, text);
  lv_obj_center(t);
  return b;
}

lv_obj_t* make_caption(lv_obj_t* parent, int y, const char* text) {
  lv_obj_t* l = make_label(parent, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_set_pos(l, 24, y);
  lv_label_set_text(l, text);
  return l;
}

void build_settings_screens(lv_obj_t* screen) {
  set_root = make_screen_root(screen, "Einstellungen");
  // Scrollable list: eight rows do not fit on one screen.
  lv_obj_t* set_list = make_list(set_root, 56, 404);
  constexpr int kRows = 9;
  const char* names[kRows] = {"WLAN", "Wetter-Ort", "Bildschirm", "Uhrzeit", "Kameras", "\xC3\x96V", "Kalender",
                              "Erinnerungen", "Wartung"};
  const char* icons[kRows] = {ICON_WIFI3, ICON_PLACE, ICON_SCREEN, ICON_SCHEDULE, ICON_VIDEOCAM, ICON_TRAIN,
                              ICON_EVENT, ICON_NOTIFY, ICON_MEMORY};
  const uint32_t colors[kRows] = {kAccent, kAmber, kPurple, kGreen, kTeal, kAccent, kPurple, kAmber, kOrange};
  lv_obj_t** vals[kRows] = {&set_wifi_val, &set_loc_val, &set_disp_val, &set_time_val, &set_cam_val,
                            &set_tr_val,   &set_cal_val, &set_rem_val,  &set_maint_val};
  const char* keys[kRows] = {"set:wifi", "set:loc", "set:disp", "set:time", "set:cams",
                             "set:oev",  "set:cal", "set:rem",  "set:maint"};
  for (int i = 0; i < kRows; ++i) {
    lv_obj_t* row = make_row(set_list, 72, icons[i], colors[i], on_settings_row, i);
    reg(keys[i], row);
    lv_obj_t* name = make_label(row, &font_inter_sb16, lv_color_hex(kText));
    lv_label_set_text(name, names[i]);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 56, 13);
    *vals[i] = make_label(row, &font_inter_14, lv_color_hex(kTextDim));
    lv_obj_set_width(*vals[i], 320);
    lv_label_set_long_mode(*vals[i], LV_LABEL_LONG_DOT);
    lv_obj_align(*vals[i], LV_ALIGN_BOTTOM_LEFT, 56, -13);
    lv_obj_t* chev = make_label(row, &font_icons_24, lv_color_hex(kTextDim));
    lv_label_set_text(chev, ICON_CHEVRON);
    lv_obj_align(chev, LV_ALIGN_RIGHT_MID, -14, 0);
  }
  lv_obj_t* pad = lv_obj_create(set_list);  // room above the home bar at the end of the list
  lv_obj_set_size(pad, 10, 8);
  style_plain(pad);

  // Bildschirm: timeout, screensaver and the black-mode hint.
  disp_root = make_screen_root(screen, "Bildschirm");
  make_caption(disp_root, 58, "Ausschalten nach");
  for (int i = 0; i < kTimeoutChoiceN; ++i) {
    char key[8];
    snprintf(key, sizeof(key), "to:%d", i);
    disp_to_btn[i] = make_choice(disp_root, kMargin + (i % 3) * 148, 80 + (i / 3) * 60, 136, 50,
                                 timeout_text(kTimeoutChoices[i]), on_timeout_btn, i, key);
  }
  make_caption(disp_root, 206, "Bildschirmschoner");
  static const char* kSaverLabel[3] = {"Schwarz", "Digital", "Analog"};
  for (int i = 0; i < 3; ++i) {
    char key[10];
    snprintf(key, sizeof(key), "saver:%d", i);
    disp_saver_btn[i] = make_choice(disp_root, kMargin + i * 148, 228, 136, 56, kSaverLabel[i], on_saver_btn, i, key);
  }
  lv_obj_t* hint_row = make_row(disp_root, 84, nullptr, 0, on_hint_row, 0);
  lv_obj_set_pos(hint_row, kMargin, 300);
  reg("disp:hint", hint_row);
  lv_obj_t* hn = make_label(hint_row, &font_inter_sb16, lv_color_hex(kText));
  lv_label_set_text(hn, "Hinweis anzeigen");
  lv_obj_align(hn, LV_ALIGN_TOP_LEFT, 16, 16);
  lv_obj_t* hd = make_label(hint_row, &font_inter_14, lv_color_hex(kTextDim));
  lv_label_set_text(hd, "Bei Schwarz: dezentes \"wake me up\"");
  lv_obj_set_width(hd, 320);
  lv_label_set_long_mode(hd, LV_LABEL_LONG_DOT);
  lv_obj_align(hd, LV_ALIGN_BOTTOM_LEFT, 16, -16);
  disp_hint_sw = lv_switch_create(hint_row);
  lv_obj_set_size(disp_hint_sw, 60, 32);
  lv_obj_align(disp_hint_sw, LV_ALIGN_RIGHT_MID, -14, 0);
  lv_obj_clear_flag(disp_hint_sw, LV_OBJ_FLAG_CLICKABLE);  // the whole row toggles
  lv_obj_set_style_bg_color(disp_hint_sw, lv_color_hex(kBorder), static_cast<lv_style_selector_t>(LV_PART_MAIN));
  lv_obj_set_style_bg_color(disp_hint_sw, lv_color_hex(kAccent), static_cast<lv_style_selector_t>(LV_PART_INDICATOR) | LV_STATE_CHECKED);

  // Uhrzeit: NTP server and time zone.
  time_root = make_screen_root(screen, "Uhrzeit");
  make_caption(time_root, 58, "Zeitserver (NTP)");
  for (int i = 0; i < 4; ++i) {
    char key[8];
    snprintf(key, sizeof(key), "ntp:%d", i);
    time_srv_btn[i] = make_choice(time_root, kMargin + (i % 2) * 226, 80 + (i / 2) * 56, 214, 48, kNtpServers[i],
                                  on_srv_btn, i, key);
  }
  time_ta = make_ta(time_root, 194, "Eigener Server");
  lv_obj_set_height(time_ta, 50);
  reg("ntp:custom", time_ta);
  make_caption(time_root, 258, "Zeitzone");
  for (int i = 0; i < 4; ++i) {
    char key[8];
    snprintf(key, sizeof(key), "tz:%d", i);
    time_tz_btn[i] = make_choice(time_root, kMargin + (i % 2) * 226, 280 + (i / 2) * 56, 214, 48, kTzChoices[i].label,
                                 on_tz_btn, i, key);
  }
  time_status = make_label(time_root, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_set_pos(time_status, 24, 404);
  lv_obj_set_width(time_status, 432);
  lv_label_set_long_mode(time_status, LV_LABEL_LONG_DOT);

  wl_root = make_screen_root(screen, "WLAN");
  wl_status = make_label(wl_root, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_set_pos(wl_status, 24, 48);
  wl_list = make_list(wl_root, 76, 380);

  wp_root = make_screen_root(screen, "WLAN");
  wp_title = lv_obj_get_child(wp_root, 0);
  wp_ta = make_ta(wp_root, 56, "Passwort");
  lv_textarea_set_password_mode(wp_ta, true);
  lv_textarea_set_password_bullet(wp_ta, "*");
  wp_status = make_label(wp_root, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_set_pos(wp_status, 24, 118);
  lv_obj_set_width(wp_status, 432);
  lv_label_set_long_mode(wp_status, LV_LABEL_LONG_WRAP);

  loc_root = make_screen_root(screen, "Wetter-Ort");
  loc_ta = make_ta(loc_root, 56, "Ort suchen");
  loc_status = make_label(loc_root, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_set_pos(loc_status, 24, 116);
  loc_list = make_list(loc_root, 140, 316);

  kb = lv_keyboard_create(screen);
  lv_obj_set_size(kb, kScreen, 220);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_obj_set_style_text_font(kb, &lv_font_montserrat_16, 0);
  lv_obj_add_event_cb(kb, on_kb_event, LV_EVENT_ALL, nullptr);
  hide(kb);
}

bool obj_visible(lv_obj_t* o) {
  for (; o != nullptr; o = lv_obj_get_parent(o)) {
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Kameras: Windy webcams, one per page, 480x480 cover-scaled. The network
// task downloads and scales into the frame that is not on screen; the UI then
// switches the image source. Refresh every kCamRefreshMs while visible.

constexpr uint32_t kCamRefreshMs = 120000;
constexpr uint32_t kCamRetryMs = 30000;
Webcam cams[kMaxCams];
int cam_n = 0;
int cam_idx = 0;
uint16_t* cam_frame[2] = {nullptr, nullptr};
lv_img_dsc_t cam_dsc[2];
int cam_shown = -1;          // frame on screen, -1 none
uint32_t cam_shown_id = 0;   // webcam in that frame
CamInfo cam_info = {};       // details of cam_shown_id
bool cam_inflight = false;
uint32_t cam_next_ms = 0;
char cam_err[96] = "";
lv_obj_t* cam_img = nullptr;
lv_obj_t* cam_card = nullptr;
lv_obj_t* cam_title = nullptr;
lv_obj_t* cam_place = nullptr;
lv_obj_t* cam_age = nullptr;
lv_obj_t* cam_link = nullptr;
lv_obj_t* cam_pill = nullptr;
lv_obj_t* cam_page = nullptr;
lv_obj_t* cam_status = nullptr;
// settings
lv_obj_t* camset_list = nullptr;
lv_obj_t* camset_status = nullptr;
lv_obj_t* camid_ta = nullptr;
lv_obj_t* camid_status = nullptr;
lv_obj_t* camsearch_ta = nullptr;
lv_obj_t* camsearch_status = nullptr;
lv_obj_t* camsearch_list = nullptr;
lv_obj_t* camkey_cur = nullptr;
lv_obj_t* camkey_ta = nullptr;
lv_obj_t* camkey_status = nullptr;
CamInfo* cam_found = nullptr;  // PSRAM, 20 entries
int cam_found_n = 0;
bool cam_busy = false;       // a camera settings request is queued or running
bool key_testing = false;    // CamLookup after a key change
char cam_added[64] = "";

const char* key_error(int status) {
  if (status == 401 || status == 403) return "API-Key ung\xC3\xBCltig (Einstellungen > Kameras)";
  if (status == 404) return "Webcam nicht gefunden";
  if (status == 429) return "Zu viele Anfragen (Windy-Limit)";
  return nullptr;
}

void cam_hide_all() {
  hide(camset_root);
  hide(camid_root);
  hide(camsearch_root);
  hide(camkey_root);
}

void cam_age_text(char* out, size_t n, int64_t updated) {
  time_t now = time(nullptr);
  if (updated <= 0 || !clock_synced()) {
    out[0] = '\0';
    return;
  }
  int64_t d = static_cast<int64_t>(now) - updated;
  if (d < 0) d = 0;
  if (d < 60) snprintf(out, n, "gerade eben");
  else if (d < 3600) snprintf(out, n, "vor %d Min", static_cast<int>(d / 60));
  else if (d < 86400) snprintf(out, n, "vor %d Std", static_cast<int>(d / 3600));
  else snprintf(out, n, "vor %d Tagen", static_cast<int>(d / 86400));
}

void cam_refresh_view() {
  if (cam_root == nullptr) return;
  char buf[96];
  if (cam_n == 0) {
    hide(cam_img);
    hide(cam_card);
    hide(cam_pill);
    lv_label_set_text(cam_status, "Keine Kameras.\nEinstellungen > Kameras");
    show(cam_status);
    return;
  }
  if (cam_idx >= cam_n) cam_idx = cam_n - 1;
  const Webcam& w = cams[cam_idx];
  char fallback[24];
  snprintf(fallback, sizeof(fallback), "Webcam %lu", static_cast<unsigned long>(w.id));
  const char* wtitle = w.title[0] != '\0' ? w.title : fallback;
  lv_label_set_text_fmt(cam_link, "windy.com/webcams/%lu", static_cast<unsigned long>(w.id));
  lv_obj_align(cam_link, LV_ALIGN_TOP_RIGHT, -16, 46);
  bool have = cam_shown >= 0 && cam_shown_id == w.id;
  snprintf(buf, sizeof(buf), "%d / %d", cam_idx + 1, cam_n);
  lv_label_set_text(cam_page, buf);
  show(cam_pill);
  if (have) {
    lv_img_set_src(cam_img, &cam_dsc[cam_shown]);
    show(cam_img);
    lv_label_set_text(cam_title, cam_info.title[0] != '\0' ? cam_info.title : wtitle);
    snprintf(buf, sizeof(buf), "%s%s%s", cam_info.city, (cam_info.city[0] && cam_info.region[0]) ? ", " : "",
             cam_info.region);
    lv_label_set_text(cam_place, buf);
    cam_age_text(buf, sizeof(buf), cam_info.updated);
    lv_label_set_text(cam_age, buf);
    show(cam_card);
    if (cam_err[0] != '\0') {  // refresh failed: keep the last picture, note the error
      lv_label_set_text(cam_status, cam_err);
      show(cam_status);
    } else {
      hide(cam_status);
    }
  } else {
    hide(cam_img);
    lv_label_set_text(cam_title, wtitle);
    lv_label_set_text(cam_place, "");
    lv_label_set_text(cam_age, "");
    show(cam_card);
    lv_label_set_text(cam_status, cam_err[0] != '\0' ? cam_err : "Bild wird geladen ...");
    show(cam_status);
  }
}

void cam_enter() {
  if (message_open || cam_root == nullptr) return;
  nav = Nav::Cameras;
  hide(home_root);
  hide(cat_root);
  hide(wx_root);
  hide_settings();
  show(cam_root);
  cam_err[0] = '\0';
  if (cam_idx >= cam_n) cam_idx = 0;
  cam_next_ms = now_ms();  // always fetch fresh on entry
  cam_refresh_view();
}

void cam_swipe(int dir) {
  int next = cam_idx + dir;
  if (next < 0 || next >= cam_n) return;
  cam_idx = next;
  cam_err[0] = '\0';
  cam_next_ms = now_ms();
  cam_refresh_view();
}

// Once a second from clock_tick: the "vor X Min" text.
void cam_tick() {
  static uint32_t last = 0;
  if (nav != Nav::Cameras || scr_off || now_ms() - last < 15000) return;
  last = now_ms();
  if (cam_shown >= 0 && cam_n > 0 && cam_shown_id == cams[cam_idx].id) {
    char buf[32];
    cam_age_text(buf, sizeof(buf), cam_info.updated);
    lv_label_set_text(cam_age, buf);
  }
}

void camset_value() {
  if (set_cam_val == nullptr) return;
  char key[72];
  bool own = false;
  bool have = settings_load_windy_key(key, sizeof(key), &own);
  char buf[96];
  snprintf(buf, sizeof(buf), "%d Kamera%s \xC2\xB7 Key: %s", cam_n, cam_n == 1 ? "" : "s",
           !have ? "fehlt" : own ? "eigener" : "Standard");
  lv_label_set_text(set_cam_val, buf);
}

void queue_cams_save() {
  SettingsRequest r = {};
  r.kind = SettingsRequest::SetCams;
  r.cam_n = cam_n;
  memcpy(r.cams, cams, sizeof(Webcam) * cam_n);
  cam_busy = true;
  queue_settings(r);
}

void on_cam_del(lv_event_t* e);
void fill_camset_list() {
  unreg_prefix("cam:");
  lv_obj_clean(camset_list);
  for (int i = 0; i < cam_n; ++i) {
    lv_obj_t* row = lv_obj_create(camset_list);
    lv_obj_set_size(row, kScreen - 2 * kMargin, 60);
    style_glass(row, 16);
    // Clickable (no action) so a drag on it scrolls the list: make_list's
    // container is not clickable, and LVGL scrolls from the pressed object up.
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    watch(row);
    lv_obj_t* ic = make_label(row, &font_icons_24, lv_color_hex(kTeal));
    lv_label_set_text(ic, ICON_VIDEOCAM);
    lv_obj_align(ic, LV_ALIGN_LEFT_MID, 14, 0);
    lv_obj_t* t = make_label(row, &font_inter_sb16, lv_color_hex(kText));
    lv_obj_set_width(t, 300);
    lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
    lv_obj_set_height(t, 20);  // one line, ellipsis
    if (cams[i].title[0] != '\0') lv_label_set_text(t, cams[i].title);
    else lv_label_set_text_fmt(t, "Webcam %lu", static_cast<unsigned long>(cams[i].id));
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 50, 9);
    lv_obj_t* id = make_label(row, &font_inter_14, lv_color_hex(kTextDim));
    lv_label_set_text_fmt(id, "ID %lu", static_cast<unsigned long>(cams[i].id));
    lv_obj_align(id, LV_ALIGN_BOTTOM_LEFT, 50, -8);
    lv_obj_t* del = lv_btn_create(row);
    lv_obj_set_size(del, 48, 44);
    lv_obj_align(del, LV_ALIGN_RIGHT_MID, -8, 0);
    style_glass(del, 12);
    lv_obj_add_event_cb(del, on_cam_del, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<intptr_t>(i)));
    watch(del);
    lv_obj_t* di = make_label(del, &font_icons_24, lv_color_hex(kRed));
    lv_label_set_text(di, ICON_DELETE);
    lv_obj_center(di);
    char key[16];
    snprintf(key, sizeof(key), "cam:del:%d", i);
    reg(key, del);
  }
}

void open_camset() {
  nav = Nav::CamSettings;
  hide(home_root);
  hide(cam_root);
  hide_settings();
  fill_camset_list();
  if (cam_added[0] != '\0') {
    lv_label_set_text_fmt(camset_status, "Hinzugef\xC3\xBCgt: %s", cam_added);
    cam_added[0] = '\0';
  } else {
    lv_label_set_text_fmt(camset_status, "%d von %d Kameras", cam_n, kMaxCams);
  }
  show(camset_root);
}

void on_cam_del(lv_event_t* e) {
  if (gesture_consumed || settings_pending) return;
  int i = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  if (i < 0 || i >= cam_n) return;
  console_printf("Remove webcam %d %lu\n", i, static_cast<unsigned long>(cams[i].id));
  char title[64];
  snprintf(title, sizeof(title), "%s", cams[i].title);
  memmove(&cams[i], &cams[i + 1], sizeof(Webcam) * (cam_n - i - 1));
  cam_n--;
  if (cam_idx >= cam_n) cam_idx = cam_n > 0 ? cam_n - 1 : 0;
  queue_cams_save();
  // Rebuild after this event returns (the button is deleted with the list).
  lv_timer_t* t = lv_timer_create([](lv_timer_t*) { open_camset(); }, 2 * kFlashMs, nullptr);
  lv_timer_set_repeat_count(t, 1);
  lv_label_set_text_fmt(camset_status, "Entfernt: %s", title);
}

bool cam_add(uint32_t id, const char* title) {
  for (int i = 0; i < cam_n; ++i) {
    if (cams[i].id == id) return false;
  }
  if (cam_n >= kMaxCams) return false;
  cams[cam_n].id = id;
  snprintf(cams[cam_n].title, sizeof(cams[cam_n].title), "%s", title);
  cam_n++;
  queue_cams_save();
  return true;
}

void show_kb_mode(lv_obj_t* ta, lv_keyboard_mode_t mode) {
  show_keyboard(ta);
  lv_keyboard_set_mode(kb, mode);
}

void on_camset_btn(lv_event_t* e) {
  if (gesture_consumed) return;
  int which = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  console_printf("Activate camset %d\n", which);
  hide_settings();
  if (which == 0) {
    nav = Nav::CamId;
    lv_textarea_set_text(camid_ta, "");
    lv_label_set_text(camid_status,
                      cam_n >= kMaxCams ? "Liste voll (max. 12)" : "Windy-ID eingeben (z.B. aus windy.com/webcams/<ID>)");
    show(camid_root);
    show_kb_mode(camid_ta, LV_KEYBOARD_MODE_NUMBER);
  } else if (which == 1) {
    nav = Nav::CamSearch;
    cam_found_n = 0;
    unreg_prefix("found:");
    lv_obj_clean(camsearch_list);
    lv_textarea_set_text(camsearch_ta, "");
    lv_label_set_text(camsearch_status, "Ortsname eingeben: Webcams im Umkreis von 25 km");
    show(camsearch_root);
    show_keyboard(camsearch_ta);
  } else {
    nav = Nav::CamKey;
    char key[72], mask[32];
    bool own = false;
    bool have = settings_load_windy_key(key, sizeof(key), &own);
    settings_mask_key(key, mask, sizeof(mask));
    memset(key, 0, sizeof(key));
    if (!have) lv_label_set_text(camkey_cur, "Kein Key hinterlegt");
    else lv_label_set_text_fmt(camkey_cur, "%s: %s", own ? "Eigener Key" : "Standard-Key (Firmware)", mask);
    lv_textarea_set_text(camkey_ta, "");
    lv_label_set_text(camkey_status, "Gratis-Key: api.windy.com/keys (Webcams API)");
    show(camkey_root);
  }
}

void on_found_row(lv_event_t* e) {
  if (gesture_consumed || settings_pending) return;
  int i = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  if (i < 0 || i >= cam_found_n) return;
  console_printf("Add found webcam %d %lu\n", i, static_cast<unsigned long>(cam_found[i].id));
  if (cam_add(cam_found[i].id, cam_found[i].title)) {
    lv_label_set_text_fmt(camsearch_status, "Hinzugef\xC3\xBCgt: %s", cam_found[i].title);
    lv_obj_t* row = lv_event_get_target(e);
    lv_obj_t* chk = make_label(row, &font_icons_24, lv_color_hex(kGreen));
    lv_label_set_text(chk, ICON_CHECK);
    lv_obj_align(chk, LV_ALIGN_RIGHT_MID, -14, 0);
  } else {
    lv_label_set_text(camsearch_status, cam_n >= kMaxCams ? "Liste voll (max. 12)" : "Schon in der Liste");
  }
}

void fill_found_list() {
  unreg_prefix("found:");
  lv_obj_clean(camsearch_list);
  for (int i = 0; i < cam_found_n; ++i) {
    lv_obj_t* row = make_row(camsearch_list, 60, ICON_VIDEOCAM, kTeal, on_found_row, i);
    lv_obj_t* t = make_label(row, &font_inter_sb16, lv_color_hex(kText));
    lv_obj_set_width(t, 330);
    lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
    lv_obj_set_height(t, 20);  // one line, ellipsis
    lv_label_set_text(t, cam_found[i].title);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 54, 9);
    lv_obj_t* d = make_label(row, &font_inter_14, lv_color_hex(kTextDim));
    lv_label_set_text_fmt(d, "%s \xC2\xB7 %d.%d km \xC2\xB7 ID %lu", cam_found[i].city,
                          static_cast<int>(cam_found[i].km), static_cast<int>(cam_found[i].km * 10) % 10,
                          static_cast<unsigned long>(cam_found[i].id));
    lv_obj_align(d, LV_ALIGN_BOTTOM_LEFT, 54, -8);
    for (int k = 0; k < cam_n; ++k) {
      if (cams[k].id == cam_found[i].id) {
        lv_obj_t* chk = make_label(row, &font_icons_24, lv_color_hex(kGreen));
        lv_label_set_text(chk, ICON_CHECK);
        lv_obj_align(chk, LV_ALIGN_RIGHT_MID, -14, 0);
      }
    }
    char key[16];
    snprintf(key, sizeof(key), "found:%d", i);
    reg(key, row);
  }
}

void on_key_default(lv_event_t*) {
  if (gesture_consumed || settings_pending) return;
  SettingsRequest r = {};
  r.kind = SettingsRequest::SetWindyKey;
  cam_busy = true;
  lv_label_set_text(camkey_status, "Speichere ...");
  queue_settings(r);
}

bool cam_kb_ready(lv_obj_t* ta) {
  if (ta == nullptr || settings_pending) return false;
  const char* text = lv_textarea_get_text(ta);
  if (ta == camid_ta) {
    unsigned long id = strtoul(text, nullptr, 10);
    if (id == 0) {
      lv_label_set_text(camid_status, "Bitte eine Zahl eingeben");
      return true;
    }
    for (int i = 0; i < cam_n; ++i) {
      if (cams[i].id == id) {
        lv_label_set_text(camid_status, "Schon in der Liste");
        return true;
      }
    }
    if (cam_n >= kMaxCams) {
      lv_label_set_text(camid_status, "Liste voll (max. 12)");
      return true;
    }
    SettingsRequest r = {};
    r.kind = SettingsRequest::CamLookup;
    r.cam_id = static_cast<uint32_t>(id);
    key_testing = false;
    cam_busy = true;
    lv_label_set_text(camid_status, "Pr\xC3\xBC" "fe Webcam ...");
    queue_settings(r);
  } else if (ta == camsearch_ta) {
    if (text[0] == '\0') return true;
    SettingsRequest r = {};
    r.kind = SettingsRequest::CamNearby;
    snprintf(r.query, sizeof(r.query), "%s", text);
    cam_busy = true;
    lv_label_set_text(camsearch_status, "Suche ...");
    queue_settings(r);
  } else if (ta == camkey_ta) {
    size_t len = strlen(text);
    if (len < 16 || len >= 72) {
      lv_label_set_text(camkey_status, "Key wirkt unvollst\xC3\xA4ndig (16-71 Zeichen)");
      return true;
    }
    SettingsRequest r = {};
    r.kind = SettingsRequest::SetWindyKey;
    snprintf(r.key, sizeof(r.key), "%s", text);
    lv_textarea_set_text(camkey_ta, "");  // do not keep the key in the widget
    cam_busy = true;
    lv_label_set_text(camkey_status, "Speichere ...");
    queue_settings(r);
  } else {
    return false;
  }
  lv_keyboard_set_textarea(kb, nullptr);
  hide(kb);
  return true;
}

bool cam_describe(char* out, size_t n) {
  switch (nav) {
    case Nav::Cameras:
      snprintf(out, n, "cameras %d/%d id=%lu img=%d loading=%d err=%s", cam_n > 0 ? cam_idx + 1 : 0, cam_n,
               cam_n > 0 ? static_cast<unsigned long>(cams[cam_idx].id) : 0UL,
               (cam_shown >= 0 && cam_n > 0 && cam_shown_id == cams[cam_idx].id) ? 1 : 0, cam_inflight ? 1 : 0,
               cam_err[0] != '\0' ? cam_err : "-");
      return true;
    case Nav::CamSettings:
      snprintf(out, n, "camset %d busy=%d", cam_n, (cam_busy || settings_pending) ? 1 : 0);
      return true;
    case Nav::CamId:
      snprintf(out, n, "camid busy=%d", (cam_busy || settings_pending) ? 1 : 0);
      return true;
    case Nav::CamSearch:
      snprintf(out, n, "camsearch %d busy=%d", cam_found_n, (cam_busy || settings_pending) ? 1 : 0);
      return true;
    case Nav::CamKey: {
      char key[72];
      bool own = false;
      settings_load_windy_key(key, sizeof(key), &own);
      memset(key, 0, sizeof(key));
      snprintf(out, n, "camkey own=%d busy=%d", own ? 1 : 0, (cam_busy || settings_pending || key_testing) ? 1 : 0);
      return true;
    }
    default:
      return false;
  }
}

bool cam_list_dump() {
  if (nav == Nav::CamSettings || nav == Nav::Cameras) {
    for (int i = 0; i < cam_n; ++i) console_printf("ITEM %d %lu %s\n", i, static_cast<unsigned long>(cams[i].id), cams[i].title);
  } else if (nav == Nav::CamSearch) {
    for (int i = 0; i < cam_found_n; ++i)
      console_printf("ITEM %d %lu %.1fkm %s|%s\n", i, static_cast<unsigned long>(cam_found[i].id), cam_found[i].km,
                     cam_found[i].title, cam_found[i].city);
  } else {
    return false;
  }
  console_printf("%s", "ITEM_END\n");
  return true;
}

lv_obj_t* make_pill_label(lv_obj_t* parent, lv_obj_t** label) {
  lv_obj_t* pill = lv_obj_create(parent);
  lv_obj_set_size(pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  style_glass(pill, 14);
  lv_obj_set_style_bg_color(pill, lv_color_hex(0x0B0F1A), 0);
  lv_obj_set_style_bg_opa(pill, 170, 0);
  lv_obj_set_style_pad_hor(pill, 12, 0);
  lv_obj_set_style_pad_ver(pill, 6, 0);
  lv_obj_clear_flag(pill, LV_OBJ_FLAG_CLICKABLE);
  *label = make_label(pill, &font_inter_14, lv_color_hex(kText));
  return pill;
}

void build_camera_screens(lv_obj_t* screen) {
  cam_n = settings_load_cams(cams, kMaxCams);
  cam_found = static_cast<CamInfo*>(heap_caps_calloc(20, sizeof(CamInfo), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  for (int i = 0; i < 2; ++i) {
    cam_frame[i] = static_cast<uint16_t*>(
        heap_caps_calloc(kCamFrameW * kCamFrameH, sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    memset(&cam_dsc[i], 0, sizeof(cam_dsc[i]));
    cam_dsc[i].header.always_zero = 0;
    cam_dsc[i].header.cf = LV_IMG_CF_TRUE_COLOR;
    cam_dsc[i].header.w = kCamFrameW;
    cam_dsc[i].header.h = kCamFrameH;
    cam_dsc[i].data_size = kCamFrameW * kCamFrameH * sizeof(uint16_t);
    cam_dsc[i].data = reinterpret_cast<const uint8_t*>(cam_frame[i]);
  }
  if (cam_frame[0] == nullptr || cam_frame[1] == nullptr) console_puts("Webcam frames: out of PSRAM");

  // Fullscreen viewer
  cam_root = lv_obj_create(screen);
  lv_obj_set_pos(cam_root, 0, 0);
  lv_obj_set_size(cam_root, kScreen, kScreen);
  style_plain(cam_root);
  lv_obj_set_style_bg_color(cam_root, lv_color_hex(0x05070C), 0);
  lv_obj_set_style_bg_opa(cam_root, LV_OPA_COVER, 0);
  cam_img = lv_img_create(cam_root);
  // The frame centres the picture; shift it up so a 16:9 preview sits between
  // the top pills and the overlay card instead of under the card.
  lv_obj_set_pos(cam_img, 0, -40);
  lv_obj_set_size(cam_img, kScreen, kScreen);
  lv_obj_clear_flag(cam_img, LV_OBJ_FLAG_CLICKABLE);
  hide(cam_img);

  cam_status = make_label(cam_root, &font_inter_16, lv_color_hex(kTextDim));
  lv_obj_set_width(cam_status, 400);
  lv_obj_set_style_text_align(cam_status, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(cam_status, LV_LABEL_LONG_WRAP);
  lv_obj_align(cam_status, LV_ALIGN_CENTER, 0, -40);

  cam_pill = make_pill_label(cam_root, &cam_page);
  lv_obj_align(cam_pill, LV_ALIGN_TOP_RIGHT, -16, 16);
  lv_obj_t* tag_lbl = nullptr;
  lv_obj_t* tag = make_pill_label(cam_root, &tag_lbl);
  lv_obj_set_flex_flow(tag, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(tag, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(tag, 6, 0);
  lv_obj_t* tag_icon = make_label(tag, &font_icons_24, lv_color_hex(kTeal));
  lv_label_set_text(tag_icon, ICON_VIDEOCAM);
  lv_obj_move_to_index(tag_icon, 0);
  lv_label_set_text(tag_lbl, "Kameras");
  lv_obj_align(tag, LV_ALIGN_TOP_LEFT, 16, 16);

  // Overlay card: title, place, capture age, Windy attribution.
  cam_card = lv_obj_create(cam_root);
  lv_obj_set_pos(cam_card, 16, 352);
  lv_obj_set_size(cam_card, kScreen - 32, 104);
  style_glass(cam_card, 20);
  lv_obj_set_style_bg_color(cam_card, lv_color_hex(0x0B0F1A), 0);
  lv_obj_set_style_bg_opa(cam_card, 175, 0);
  lv_obj_clear_flag(cam_card, LV_OBJ_FLAG_CLICKABLE);
  cam_title = make_label(cam_card, &font_inter_20, lv_color_hex(kText));
  lv_obj_set_width(cam_title, kScreen - 32 - 36);
  lv_label_set_long_mode(cam_title, LV_LABEL_LONG_DOT);
  lv_obj_set_height(cam_title, 26);
  lv_obj_set_pos(cam_title, 18, 12);
  cam_place = make_label(cam_card, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_set_width(cam_place, 200);
  lv_label_set_long_mode(cam_place, LV_LABEL_LONG_DOT);
  lv_obj_set_pos(cam_place, 18, 44);
  cam_age = make_label(cam_card, &font_inter_sb16, lv_color_hex(kAmber));
  lv_obj_set_pos(cam_age, 18, 68);
  // Windy terms: courtesy "Webcams provided by windy.com" in the corner, and
  // every image linked to its webcam page (shown as text on this panel).
  cam_link = make_label(cam_card, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_align(cam_link, LV_ALIGN_TOP_RIGHT, -16, 46);
  lv_obj_t* attr = make_label(cam_card, &font_inter_14, lv_color_hex(kTextDim));
  lv_label_set_text(attr, "Webcams provided by windy.com");
  lv_obj_align(attr, LV_ALIGN_BOTTOM_RIGHT, -16, -12);
  make_home_bar(cam_root);
  hide(cam_root);

  // Einstellungen > Kameras
  camset_root = make_screen_root(screen, "Kameras");
  static const char* kBtn[3] = {"+ ID", "Ort suchen", "API-Key"};
  static const char* kKey[3] = {"camset:id", "camset:search", "camset:key"};
  for (int i = 0; i < 3; ++i) make_choice(camset_root, kMargin + i * 148, 56, 136, 48, kBtn[i], on_camset_btn, i, kKey[i]);
  camset_status = make_label(camset_root, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_set_pos(camset_status, 24, 114);
  lv_obj_set_width(camset_status, 432);
  lv_label_set_long_mode(camset_status, LV_LABEL_LONG_DOT);
  camset_list = make_list(camset_root, 138, 318);

  camid_root = make_screen_root(screen, "Webcam per ID");
  camid_ta = make_ta(camid_root, 56, "Windy Webcam-ID");
  lv_textarea_set_accepted_chars(camid_ta, "0123456789");
  lv_textarea_set_max_length(camid_ta, 10);
  reg("camid:ta", camid_ta);
  camid_status = make_label(camid_root, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_set_pos(camid_status, 24, 118);
  lv_obj_set_width(camid_status, 432);
  lv_label_set_long_mode(camid_status, LV_LABEL_LONG_WRAP);

  camsearch_root = make_screen_root(screen, "Webcam suchen");
  camsearch_ta = make_ta(camsearch_root, 56, "Ort suchen");
  reg("camsearch:ta", camsearch_ta);
  camsearch_status = make_label(camsearch_root, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_set_pos(camsearch_status, 24, 116);
  lv_obj_set_width(camsearch_status, 432);
  lv_label_set_long_mode(camsearch_status, LV_LABEL_LONG_DOT);
  camsearch_list = make_list(camsearch_root, 140, 316);

  camkey_root = make_screen_root(screen, "Windy API-Key");
  make_caption(camkey_root, 58, "Aktuell");
  camkey_cur = make_label(camkey_root, &font_inter_sb16, lv_color_hex(kText));
  lv_obj_set_pos(camkey_cur, 24, 80);
  lv_obj_set_width(camkey_cur, 432);
  lv_label_set_long_mode(camkey_cur, LV_LABEL_LONG_DOT);
  camkey_ta = make_ta(camkey_root, 112, "Eigenen Key eingeben");
  lv_textarea_set_password_mode(camkey_ta, true);
  lv_textarea_set_password_bullet(camkey_ta, "*");
  lv_textarea_set_max_length(camkey_ta, 71);
  reg("camkey:ta", camkey_ta);
  lv_obj_t* def = make_choice(camkey_root, kMargin, 178, kScreen - 2 * kMargin, 48, "Eigenen Key l\xC3\xB6schen (Standard verwenden)",
                              on_key_default, 0, "camkey:default");
  (void)def;
  camkey_status = make_label(camkey_root, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_set_pos(camkey_status, 24, 240);
  lv_obj_set_width(camkey_status, 432);
  lv_label_set_long_mode(camkey_status, LV_LABEL_LONG_WRAP);
}

#include "panel_ui_info.inc"
#include "panel_ui_rem.inc"

}  // namespace

void ui_set_week(const WeatherDetailDay* days, int count) {
  if (count > kWxDays) count = kWxDays;
  if (wx_week == nullptr) {
    wx_week = static_cast<WeatherDetailDay*>(
        heap_caps_malloc(sizeof(WeatherDetailDay) * kWxDays, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (wx_week == nullptr) return;
  }
  memcpy(wx_week, days, sizeof(WeatherDetailDay) * count);
  wx_count = count;
  if (nav == Nav::Weather) {
    refresh_weather_day();
  }
}

void ui_describe(char* out, size_t n) {
  if (!message_open && cam_describe(out, n)) return;
  if (!message_open && (info_describe(out, n) || rem_describe(out, n))) return;
  if (message_open) {
    snprintf(out, n, "message");
  } else if (nav == Nav::Home) {
    snprintf(out, n, "home");
  } else if (nav == Nav::Weather) {
    snprintf(out, n, "weather %d/%d", wx_day + 1, wx_count);
  } else if (nav == Nav::Settings) {
    snprintf(out, n, "settings");
  } else if (nav == Nav::WifiList) {
    snprintf(out, n, "wifi %d scanning=%d", ui_ap_n, wl_scanning ? 1 : 0);
  } else if (nav == Nav::WifiPass) {
    snprintf(out, n, "wifipass connecting=%d", wp_connecting ? 1 : 0);
  } else if (nav == Nav::Time) {
    snprintf(out, n, "time server=%s tz=%s synced=%d", tset.server, tset.tz_name, clock_synced() ? 1 : 0);
  } else if (nav == Nav::Display) {
    snprintf(out, n, "display timeout=%u hint=%d saver=%d", static_cast<unsigned>(disp.timeout_s), disp.hint ? 1 : 0,
             disp.saver);
  } else if (nav == Nav::Location) {
    snprintf(out, n, "location %d searching=%d", ui_place_n, (loc_searching || settings_pending) ? 1 : 0);
  } else {
    snprintf(out, n, "category %s %d/%d", section_name(current_section), section_page + 1,
             section_pages[current_section]);
  }
}

void ui_touch_sample(bool pressed, int x, int y) {
  if (pressed) {
    if (!touch_down) {
      touch_down = true;
      touch_t0 = now_ms();
      touch_max_move = 0;
      gesture_x0 = x;
      gesture_y0 = y;
      gesture_consumed = false;
    }
    int m = abs(x - gesture_x0);
    int my = abs(y - gesture_y0);
    if (my > m) {
      m = my;
    }
    if (m > touch_max_move) {
      touch_max_move = m;
    }
    if (touch_max_move > kTapSlop) {
      gesture_consumed = true;  // moved: no widget may click from here on
    }
    touch_x = x;
    touch_y = y;
  } else if (touch_down) {
    touch_down = false;
    finish_touch();
  }
}

void ui_init() {
  lv_obj_t* screen = lv_scr_act();
  lv_obj_set_style_bg_color(screen, lv_color_hex(kBgTop), 0);
  lv_obj_set_style_bg_grad_color(screen, lv_color_hex(kBgBottom), 0);
  lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
  watch(screen);

  main_screen = screen;
  tset = settings_load_time();
  clock_apply_tz(tset.tz);

  home_root = lv_obj_create(screen);
  lv_obj_set_pos(home_root, 0, 0);
  lv_obj_set_size(home_root, kScreen, kScreen);
  style_plain(home_root);

  home_date = make_label(home_root, &font_inter_16, lv_color_hex(kTextDim));
  lv_obj_set_pos(home_date, 24, 24);
  home_time = make_label(home_root, &font_inter_16, lv_color_hex(kTextDim));
  lv_label_set_text(home_time, "--:--");
  lv_obj_align(home_time, LV_ALIGN_TOP_RIGHT, -24, 24);

  // One weather card: now (icon, temperature, place and condition), then
  // today and tomorrow side by side under a divider. Tap a day for details.
  lv_obj_t* weather = lv_obj_create(home_root);
  lv_obj_set_pos(weather, 24, 56);
  lv_obj_set_size(weather, kScreen - 48, 222);
  style_glass(weather, 28);
  lv_obj_clear_flag(weather, LV_OBJ_FLAG_CLICKABLE);

  weather_big = make_label(weather, &font_icons_80, lv_color_hex(kAmber));
  lv_obj_set_pos(weather_big, 18, 18);
  weather_icon(weather_big, -1);

  weather_temp = make_label(weather, &font_inter_xl_72, lv_color_hex(kText));
  lv_obj_set_pos(weather_temp, 120, 10);
  lv_label_set_text(weather_temp, "--");

  weather_place = make_label(weather, &font_inter_16, lv_color_hex(kTextDim));
  lv_obj_set_pos(weather_place, 124, 96);
  lv_obj_set_width(weather_place, 290);
  lv_label_set_long_mode(weather_place, LV_LABEL_LONG_DOT);

  make_divider(weather, 20, 130, kScreen - 48 - 40, 1);
  make_divider(weather, (kScreen - 48) / 2, 144, 1, 62);

  static const char* kDayName[2] = {"Heute", "Morgen"};
  for (int d = 0; d < 2; ++d) {
    lv_obj_t* panel = lv_obj_create(weather);
    lv_obj_set_pos(panel, 8 + d * 208, 136);
    lv_obj_set_size(panel, 200, 78);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_10, LV_STATE_PRESSED);
    lv_obj_set_style_radius(panel, 16, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_set_style_shadow_width(panel, 0, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(panel, on_day_panel, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<intptr_t>(d)));
    watch(panel);
    reg(d == 0 ? "day:0" : "day:1", panel);

    fc_icon[d] = make_label(panel, &font_icons_38, lv_color_hex(kAmber));
    lv_obj_align(fc_icon[d], LV_ALIGN_LEFT_MID, 12, 0);
    weather_icon(fc_icon[d], -1);

    lv_obj_t* name = make_label(panel, &font_inter_14, lv_color_hex(kTextDim));
    lv_label_set_text(name, kDayName[d]);
    lv_obj_set_pos(name, 64, 8);

    fc_temp[d] = make_label(panel, &font_inter_20, lv_color_hex(kText));
    lv_label_set_recolor(fc_temp[d], true);
    lv_label_set_text(fc_temp[d], "--");
    lv_obj_set_pos(fc_temp[d], 64, 26);

    lv_obj_t* drop = make_label(panel, &font_icons_16, lv_color_hex(kAccent));
    lv_label_set_text(drop, ICON_DROP);
    lv_obj_set_pos(drop, 63, 54);
    fc_rain[d] = make_label(panel, &font_inter_14, lv_color_hex(kAccent));
    lv_label_set_text(fc_rain[d], "");
    lv_obj_set_pos(fc_rain[d], 82, 53);
  }
  update_weather_line();

  // Five icon-only buttons in one row.
  struct HomeButton {
    const char* icon;
    const char* name;
    uint32_t color;
  };
  // Six icon-only buttons in one row (432 px: 6 x 64 + 5 x 9.6).
  static const HomeButton kButtons[6] = {
      {ICON_STAR, "cat:fav", kSectionAccent[0]},      {ICON_BULB, "cat:licht", kSectionAccent[1]},
      {ICON_BLINDS, "cat:storen", kSectionAccent[2]}, {ICON_TOGGLE, "cat:schalter", kSectionAccent[3]},
      {ICON_VIDEOCAM, "cat:kameras", kTeal},          {ICON_SETTINGS, "cat:settings", kPurple},
  };
  static const int kWhich[6] = {0, 1, 2, 3, 5, 4};  // on_category: 4 = settings, 5 = cameras
  for (int i = 0; i < 6; ++i) {
    const HomeButton& b = kButtons[i];
    lv_obj_t* btn = lv_btn_create(home_root);
    lv_obj_set_pos(btn, 24 + (i * 368) / 5, 300);
    lv_obj_set_size(btn, 64, 64);
    style_tile(btn, b.color);
    lv_obj_add_event_cb(btn, on_category, LV_EVENT_CLICKED,
                        reinterpret_cast<void*>(static_cast<intptr_t>(kWhich[i])));
    watch(btn);
    reg(b.name, btn);
    card_btn[i] = btn;
    lv_obj_t* icon = make_label(btn, &font_icons_38, lv_color_hex(b.color));
    lv_label_set_text(icon, b.icon);
    lv_obj_center(icon);
  }
  build_home_info(weather);  // warning pill in the card, ÖV + Kalender chips below the buttons

  cat_root = lv_obj_create(screen);
  lv_obj_set_pos(cat_root, 0, 0);
  lv_obj_set_size(cat_root, kScreen, kScreen);
  style_plain(cat_root);

  cat_icon = make_label(cat_root, &font_icons_24, lv_color_hex(kAmber));
  lv_obj_set_pos(cat_icon, 22, 20);
  cat_title = make_label(cat_root, &font_inter_20, lv_color_hex(kText));
  lv_obj_set_pos(cat_title, 54, 21);
  lv_label_set_text(cat_title, "");

  cat_page = make_label(cat_root, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_align(cat_page, LV_ALIGN_TOP_RIGHT, -24, 25);
  hide(cat_page);

  cat_status = make_label(cat_root, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_set_pos(cat_status, 24, 54);
  lv_obj_set_width(cat_status, 432);
  lv_label_set_long_mode(cat_status, LV_LABEL_LONG_DOT);
  lv_label_set_text(cat_status, "");

  for (int slot = 0; slot < kPerPage; ++slot) {
    int col = slot % 2;
    int row = slot / 2;
    int x = kMargin + col * (kTileW + kGap);
    int y = kContentY + row * (kTileH + kGap);
    lv_obj_t* btn = lv_btn_create(cat_root);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, kTileW, kTileH);
    style_glass(btn, 22);
    lv_obj_add_event_cb(btn, on_tile, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<intptr_t>(slot)));
    watch(btn);
    tiles[slot] = btn;

    tile_icon[slot] = make_label(btn, &font_icons_32, lv_color_hex(kOffIcon));
    lv_obj_set_pos(tile_icon[slot], 16, 14);

    tile_name[slot] = make_label(btn, &font_inter_sb16, lv_color_hex(kText));
    lv_obj_set_width(tile_name[slot], kTileW - 32);
    lv_label_set_long_mode(tile_name[slot], LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(tile_name[slot], 16, 66);

    tile_sub[slot] = make_label(btn, &font_inter_14, lv_color_hex(kTextDim));
    lv_obj_set_width(tile_sub[slot], kTileW - 32);
    lv_label_set_long_mode(tile_sub[slot], LV_LABEL_LONG_DOT);
    lv_obj_align(tile_sub[slot], LV_ALIGN_BOTTOM_LEFT, 16, -14);
    hide(btn);
  }

  for (int row_slot = 0; row_slot < kRowsPerPage; ++row_slot) {
    lv_obj_t* box = lv_obj_create(cat_root);
    lv_obj_set_pos(box, kMargin, kContentY + row_slot * (kTileH + kGap));
    lv_obj_set_size(box, kScreen - 2 * kMargin, kTileH);
    style_glass(box, 22);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_CLICKABLE);
    shutter_box[row_slot] = box;

    shutter_title[row_slot] = make_label(box, &font_inter_sb16, lv_color_hex(kText));
    lv_obj_set_pos(shutter_title[row_slot], 18, 16);
    lv_obj_set_width(shutter_title[row_slot], 300);
    lv_label_set_long_mode(shutter_title[row_slot], LV_LABEL_LONG_DOT);

    shutter_pos[row_slot] = make_label(box, &font_inter_14, lv_color_hex(kAccent));
    lv_obj_align(shutter_pos[row_slot], LV_ALIGN_TOP_RIGHT, -18, 18);

    for (int which = 0; which < 2; ++which) {
      lv_obj_t* btn = lv_btn_create(box);
      lv_obj_set_pos(btn, 14 + which * 208, 52);
      lv_obj_set_size(btn, 198, 88);
      style_shutter_button(btn);
      intptr_t tag = static_cast<intptr_t>(row_slot * 2 + which);
      lv_obj_add_event_cb(btn, on_shutter, LV_EVENT_CLICKED, reinterpret_cast<void*>(tag));
      watch(btn);
      lv_obj_t* arrow = make_label(btn, &font_icons_32, lv_color_hex(kAccent));
      lv_label_set_text(arrow, kShutterIcon[which]);
      lv_obj_align(arrow, LV_ALIGN_CENTER, -28, 0);
      lv_obj_t* label = make_label(btn, &font_inter_20, lv_color_hex(kText));
      lv_label_set_text(label, kShutterLabel[which]);
      lv_obj_align(label, LV_ALIGN_CENTER, 18, 0);
    }
    hide(box);
  }

  empty_hint = make_label(cat_root, &font_inter_20, lv_color_hex(kTextDim));
  lv_obj_set_width(empty_hint, 400);
  lv_label_set_long_mode(empty_hint, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(empty_hint, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(empty_hint, LV_ALIGN_CENTER, 0, 20);
  hide(empty_hint);

  // Right-edge handle: shown by refresh_category() only when a next page
  // exists. (It used to mean "swipe back to home" and was always shown.)
  next_hint = wx_handle(cat_root, 470);
  hide(next_hint);
  prev_hint = wx_handle(cat_root, 5);
  hide(prev_hint);
  make_home_bar(cat_root);

  hide(cat_root);

  build_weather_screen(screen);

  msg_status = make_label(screen, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_set_pos(msg_status, 24, 28);
  lv_obj_set_width(msg_status, 432);
  lv_label_set_long_mode(msg_status, LV_LABEL_LONG_DOT);
  lv_label_set_text(msg_status, "");
  hide(msg_status);

  message = lv_obj_create(screen);
  lv_obj_set_pos(message, 20, 88);
  lv_obj_set_size(message, 440, 360);
  style_glass(message, 28);
  lv_obj_set_style_bg_opa(message, 210, 0);
  watch(message);

  message_title = make_label(message, &lv_font_montserrat_28, lv_color_hex(kOrange));
  lv_obj_set_width(message_title, 392);
  lv_label_set_long_mode(message_title, LV_LABEL_LONG_WRAP);
  lv_obj_align(message_title, LV_ALIGN_TOP_LEFT, 20, 16);

  message_body = make_label(message, &font_inter_20, lv_color_hex(kText));
  lv_obj_set_width(message_body, 392);
  lv_label_set_long_mode(message_body, LV_LABEL_LONG_WRAP);
  lv_obj_align(message_body, LV_ALIGN_TOP_LEFT, 20, 80);

  msg_wifi_btn = lv_btn_create(message);
  lv_obj_set_size(msg_wifi_btn, 188, 80);
  lv_obj_align(msg_wifi_btn, LV_ALIGN_BOTTOM_LEFT, 4, -16);
  style_glass(msg_wifi_btn, 20);
  lv_obj_add_event_cb(msg_wifi_btn, on_msg_wifi, LV_EVENT_CLICKED, nullptr);
  watch(msg_wifi_btn);
  reg("msg:wifi", msg_wifi_btn);
  lv_obj_t* wifi_label = make_label(msg_wifi_btn, &font_inter_20, lv_color_hex(kText));
  lv_label_set_text(wifi_label, "WLAN");
  lv_obj_center(wifi_label);

  retry_btn = lv_btn_create(message);
  lv_obj_set_size(retry_btn, 188, 80);
  lv_obj_align(retry_btn, LV_ALIGN_BOTTOM_RIGHT, -4, -16);
  reg("msg:retry", retry_btn);
  style_glass(retry_btn, 20);
  lv_obj_set_style_bg_color(retry_btn, lv_color_hex(kGreen), 0);
  lv_obj_set_style_bg_opa(retry_btn, LV_OPA_50, 0);
  lv_obj_add_event_cb(retry_btn, on_retry, LV_EVENT_CLICKED, nullptr);
  watch(retry_btn);
  lv_obj_t* retry_label = make_label(retry_btn, &font_inter_20, lv_color_hex(kText));
  lv_label_set_text(retry_label, "Try again");
  lv_obj_center(retry_label);
  hide(message);
  hide(home_root);
  build_settings_screens(screen);
  build_camera_screens(screen);
  build_info_screens(screen);
  lv_obj_move_foreground(kb);
  build_sleep_layer();
  build_reminders(screen);
  lv_timer_create(clock_tick, 1000, nullptr);
  clock_tick(nullptr);
}

bool ui_power_touch(bool pressed) {
  if (pressed) last_activity = now_ms();
  if (scr_off && pressed && rem_shown_now()) return true;  // the card's OK works directly; elsewhere wakes
  if (scr_off && pressed) {
    wake_swallow = true;
    screen_on();
    console_puts("Wake touch swallowed");
    return false;
  }
  if (wake_swallow) {
    if (!pressed) wake_swallow = false;
    return false;
  }
  return true;
}

void ui_power_describe(char* out, size_t n) {
  snprintf(out, n, "scr=%s bl=%u to=%u saver=%d rem=%d", scr_off ? "off" : "on",
           static_cast<unsigned>(panel_backlight_level()), static_cast<unsigned>(scr_timeout_s), disp.saver,
           ui_rem_pending());
}

lv_obj_t* ui_overlay() { return scr_off ? sleep_layer : nullptr; }

void ui_set_screen_timeout(uint16_t seconds) {
  scr_timeout_s = seconds;
  last_activity = now_ms();
}

void ui_show_message(const char* title, const char* body, bool retry) {
  lv_label_set_text(message_title, title != nullptr ? title : "");
  lv_label_set_text(message_body, body != nullptr ? body : "");
  if (retry) {
    show(retry_btn);
    show(msg_wifi_btn);
  } else {
    hide(retry_btn);
    hide(msg_wifi_btn);
  }
  hide_settings();
  message_open = true;
  hide(home_root);
  hide(cat_root);
  hide(wx_root);
  show(msg_status);
  show(message);
}

void ui_show_controls(const LoxoneControl* items, size_t count, size_t supported, const char* server) {
  if (count > kLoxoneControlCap) {
    count = kLoxoneControlCap;
  }
  control_count = count;
  page = -1;
  section_page = 0;
  command_pending = false;
  snprintf(server_name, sizeof(server_name), "%s", (server != nullptr && server[0] != '\0') ? server : "");
  memset(known, 0, sizeof(known));
  memset(known_on, 0, sizeof(known_on));
  memset(needs_read, 0, sizeof(needs_read));
  memset(prefer, 0, sizeof(prefer));
  memset(moving, 0, sizeof(moving));
  memset(position_pct, 0, sizeof(position_pct));
  memset(reread_after, 0, sizeof(reread_after));
  for (size_t i = 0; i < count; ++i) {
    controls[i] = items[i];
    needs_read[i] = has_status(controls[i]);
  }
  rebuild_pages();
  console_printf("Home categories ready, Schalter pages %d\n", section_pages[kSecOther]);
  hide(msg_status);
  show_home();
  ui_set_status(server_name);
  (void)supported;
}

void ui_set_status(const char* text) {
  const char* value = text != nullptr ? text : "";
  if (cat_status != nullptr) {
    lv_label_set_text(cat_status, value);
  }
  if (msg_status != nullptr) {
    lv_label_set_text(msg_status, value);
  }
}

void ui_set_forecast(bool ok, const WeatherDay* days, int count) {
  if (fc_temp[0] == nullptr) {
    return;
  }
  if (!ok) {
    if (!fc_have) {
      for (int d = 0; d < 2; ++d) {
        lv_label_set_text(fc_temp[d], "--");
        lv_label_set_text(fc_rain[d], "");
        weather_icon(fc_icon[d], -1);
      }
    }
    return;  // keep the last good forecast
  }
  fc_have = true;
  for (int d = 0; d < 2 && d < count; ++d) {
    char buf[48];
    snprintf(buf, sizeof(buf), "%d\xC2\xB0 #8A94A8 / %d\xC2\xB0#", round_c(days[d].t_max), round_c(days[d].t_min));
    lv_label_set_text(fc_temp[d], buf);
    snprintf(buf, sizeof(buf), "%.1f mm", static_cast<double>(days[d].precip));
    lv_label_set_text(fc_rain[d], buf);
    day_icon(fc_icon[d], days[d].kind);
  }
}

void ui_set_weather(bool ok, int temp_c, int code) {
  if (weather_temp == nullptr) {
    return;
  }
  if (!ok) {
    lv_label_set_text(weather_temp, "--");
    snprintf(cur_cond, sizeof(cur_cond), "Wetter nicht verf\xC3\xBCgbar");
  } else {
    char buf[12];
    snprintf(buf, sizeof(buf), "%d\xC2\xB0", temp_c);
    lv_label_set_text(weather_temp, buf);
    weather_icon(weather_big, code);
    snprintf(cur_cond, sizeof(cur_cond), "%s", weather_name(code));
  }
  update_weather_line();
}

void ui_time_done(bool ok) {
  time_saving = false;
  console_printf("Time settings saved=%d\n", ok ? 1 : 0);
  refresh_time_settings();
}

bool ui_take_command(LoxoneCommandRequest* out) {
  if (!command_pending || out == nullptr) {
    return false;
  }
  *out = pending;
  command_pending = false;
  return true;
}

bool ui_take_retry() {
  if (!retry_pending) {
    return false;
  }
  retry_pending = false;
  return true;
}

void ui_command_finished(int index, bool ok, const char* command, const char* detail) {
  if (index >= 0 && static_cast<size_t>(index) < control_count && ok && command != nullptr) {
    if (strcmp(command, "On") == 0) {
      known[index] = true;
      known_on[index] = true;
      needs_read[index] = false;
    } else if (strcmp(command, "Off") == 0) {
      known[index] = true;
      known_on[index] = false;
      needs_read[index] = false;
    } else if (controls[index].kind == kLoxoneShutter) {
      moving[index] = true;
      needs_read[index] = true;
      prefer[index] = true;
      reread_after[index] = now_ms() + kShutterRereadMs;
    }
  }
  if (!visible_pending()) {
    if (ok) {
      if (command != nullptr && strcmp(command, "Pulse") == 0) {
        ui_set_status("Impuls gesendet");
      } else if (command != nullptr && strcmp(command, "FullUp") == 0) {
        ui_set_status("Storen auf");
      } else if (command != nullptr && strcmp(command, "FullDown") == 0) {
        ui_set_status("Storen ab");
      } else {
        ui_set_status("Gesendet");
      }
    } else {
      ui_set_status(detail != nullptr && detail[0] != '\0' ? detail : "Befehl fehlgeschlagen");
    }
  }
  refresh_visible();
}

bool ui_take_state(LoxoneStateRequest* out) {
  if (out == nullptr || command_pending) {
    return false;
  }
  uint32_t now = now_ms();
  int index = pick_state(true, now);
  if (index < 0) {
    index = pick_state(false, now);
  }
  if (index < 0) {
    return false;
  }
  out->index = index;
  snprintf(out->action, sizeof(out->action), "%s", controls[index].action);
  snprintf(out->state, sizeof(out->state), "%s", controls[index].state);
  out->kind = controls[index].kind;
  return true;
}

void ui_state_finished(int index, bool ok, const char* value) {
  if (index < 0 || static_cast<size_t>(index) >= control_count) {
    return;
  }
  needs_read[index] = false;
  reread_after[index] = 0;
  moving[index] = false;
  if (ok) {
    apply_state_value(index, value);
  }
  publish_status();
  refresh_visible();
}

// --- Settings API (UI task) --------------------------------------------------

bool ui_take_settings(SettingsRequest* out) {
  if (!settings_pending) return false;
  *out = settings_req;
  settings_pending = false;
  return true;
}

void ui_wifi_scan_done(const WifiAp* aps, int n) {
  ui_ap_n = n < 0 ? 0 : (n > kMaxAps ? kMaxAps : n);
  memcpy(ui_aps, aps, sizeof(WifiAp) * ui_ap_n);
  wl_scanning = false;
  if (nav != Nav::WifiList) return;
  char t[48];
  snprintf(t, sizeof(t), ui_ap_n > 0 ? "%d Netzwerke" : "Keine Netzwerke gefunden", ui_ap_n);
  lv_label_set_text(wl_status, t);
  fill_wifi_list();
}

void ui_wifi_connect_done(bool ok, const char* ssid, const char* current) {
  wp_connecting = false;
  snprintf(cur_ssid, sizeof(cur_ssid), "%s", current != nullptr ? current : "");
  if (nav != Nav::WifiPass) return;
  if (ok) {
    open_settings();
  } else {
    char t[120];
    snprintf(t, sizeof(t), "Verbindung zu %s fehlgeschlagen. Wieder mit %s verbunden.", ssid, cur_ssid);
    lv_label_set_text(wp_status, t);
  }
}

void ui_geocode_done(bool ok, const GeoPlace* places, int n) {
  loc_searching = false;
  ui_place_n = n < 0 ? 0 : (n > kMaxPlaces ? kMaxPlaces : n);
  memcpy(ui_places, places, sizeof(GeoPlace) * ui_place_n);
  if (nav != Nav::Location) return;
  lv_label_set_text(loc_status, !ok ? "Suche fehlgeschlagen" : ui_place_n == 0 ? "Kein Ort gefunden" : "Ort antippen");
  fill_loc_list();
}

void ui_set_place(const char* name) {
  snprintf(cur_place, sizeof(cur_place), "%s", name != nullptr ? name : "");
  update_weather_line();
}

void ui_location_done(bool ok, const char* name) {
  ui_set_place(name);
  if (nav == Nav::Location || nav == Nav::Settings) open_settings();
}

void ui_set_wifi_name(const char* ssid) { snprintf(cur_ssid, sizeof(cur_ssid), "%s", ssid != nullptr ? ssid : ""); }

bool ui_find(const char* name, int* x, int* y) {
  for (int i = 0; i < named_n; ++i) {
    if (strcmp(named[i].name, name) == 0 && named[i].obj != nullptr && obj_visible(named[i].obj)) {
      lv_obj_scroll_to_view_recursive(named[i].obj, LV_ANIM_OFF);  // rows in scrolled lists
      lv_obj_update_layout(named[i].obj);
      lv_area_t a;
      lv_obj_get_coords(named[i].obj, &a);
      *x = (a.x1 + a.x2) / 2;
      *y = (a.y1 + a.y2) / 2;
      return true;
    }
  }
  return false;
}

void ui_list_dump() {
  if (cam_list_dump()) return;
  if (info_list_dump()) return;
  if (nav == Nav::WifiList) {
    for (int i = 0; i < ui_ap_n; ++i) console_printf("ITEM %d %d %s\n", i, ui_aps[i].rssi, ui_aps[i].ssid);
  } else if (nav == Nav::Location) {
    for (int i = 0; i < ui_place_n; ++i)
      console_printf("ITEM %d %.4f,%.4f %s|%s\n", i, ui_places[i].lat, ui_places[i].lon, ui_places[i].name,
                     ui_places[i].region);
  }
  console_printf("%s", "ITEM_END\n");
}

bool ui_type(const char* text) {
  lv_obj_t* ta = kb != nullptr ? lv_keyboard_get_textarea(kb) : nullptr;
  if (ta == nullptr) return false;
  lv_textarea_add_text(ta, text);
  return true;
}

bool ui_enter() {
  if (kb == nullptr || lv_keyboard_get_textarea(kb) == nullptr) return false;
  lv_event_send(kb, LV_EVENT_READY, nullptr);
  return true;
}

void ui_reset_location() {
  SettingsRequest r = {};
  r.kind = SettingsRequest::ResetLocation;
  queue_settings(r);
}

// --- Kameras ---

bool ui_take_cam(CamRequest* out) {
  if (nav != Nav::Cameras || scr_off || cam_n == 0 || cam_inflight) return false;
  if (static_cast<int32_t>(now_ms() - cam_next_ms) < 0) return false;
  int target = cam_shown == 0 ? 1 : 0;
  if (cam_frame[target] == nullptr) return false;
  out->id = cams[cam_idx].id;
  out->frame = cam_frame[target];
  cam_inflight = true;
  return true;
}

void ui_cam_done(bool ok, int status, const CamInfo& info, uint16_t* frame, const char* err) {
  cam_inflight = false;
  bool current = cam_n > 0 && info.id == cams[cam_idx].id;
  if (!current) {
    cam_next_ms = now_ms();  // swiped meanwhile: fetch the camera now on screen
    return;
  }
  if (ok) {
    cam_shown = frame == cam_frame[1] ? 1 : 0;
    cam_shown_id = info.id;
    cam_info = info;
    cam_err[0] = '\0';
    // keep the stored title in sync with Windy (lists show it)
    if (info.title[0] != '\0' && strcmp(cams[cam_idx].title, info.title) != 0) {
      snprintf(cams[cam_idx].title, sizeof(cams[cam_idx].title), "%s", info.title);
    }
    lv_obj_invalidate(cam_img);
    cam_next_ms = now_ms() + kCamRefreshMs;
  } else {
    const char* k = key_error(status);
    snprintf(cam_err, sizeof(cam_err), "%s", k != nullptr ? k : (err != nullptr && err[0] != '\0') ? err : "Fehler");
    cam_next_ms = now_ms() + kCamRetryMs;
  }
  if (nav == Nav::Cameras) cam_refresh_view();
}

void ui_cam_lookup_done(bool ok, int status, const CamInfo& info) {
  cam_busy = false;
  if (key_testing) {
    key_testing = false;
    if (camkey_status != nullptr) {
      if (ok) lv_label_set_text(camkey_status, "Gespeichert. Key funktioniert.");
      else lv_label_set_text_fmt(camkey_status, "Gespeichert, aber Windy antwortet HTTP %d", status);
    }
    return;
  }
  if (!ok) {
    const char* k = key_error(status);
    if (k != nullptr) lv_label_set_text(camid_status, k);
    else lv_label_set_text_fmt(camid_status, "Fehler (HTTP %d)", status);
    return;
  }
  if (cam_add(info.id, info.title)) {
    snprintf(cam_added, sizeof(cam_added), "%s", info.title);
    if (nav == Nav::CamId) open_camset();
  } else {
    lv_label_set_text(camid_status, "Schon in der Liste oder Liste voll");
  }
}

void ui_cam_nearby_done(bool ok, int status, const CamInfo* found, int n, const char* place) {
  cam_busy = false;
  if (n > 20) n = 20;
  cam_found_n = (ok && cam_found != nullptr) ? n : 0;
  if (cam_found_n > 0) memcpy(cam_found, found, sizeof(CamInfo) * cam_found_n);
  if (nav != Nav::CamSearch) return;
  fill_found_list();
  if (!ok) {
    const char* k = key_error(status);
    if (status == 404 && place != nullptr) lv_label_set_text_fmt(camsearch_status, "Ort \"%s\" nicht gefunden", place);
    else if (k != nullptr) lv_label_set_text(camsearch_status, k);
    else lv_label_set_text_fmt(camsearch_status, "Fehler (HTTP %d)", status);
  } else {
    lv_label_set_text_fmt(camsearch_status, "%d Webcams um %s (25 km)", cam_found_n, place != nullptr ? place : "");
  }
}

void ui_cams_saved(bool ok) {
  cam_busy = false;
  if (!ok && camset_status != nullptr) lv_label_set_text(camset_status, "Speichern fehlgeschlagen");
}

void ui_windy_key_done(bool ok) {
  cam_busy = false;
  cam_err[0] = '\0';
  cam_next_ms = now_ms();
  if (nav != Nav::CamKey) return;
  char key[72], mask[32];
  bool own = false;
  bool have = settings_load_windy_key(key, sizeof(key), &own);
  settings_mask_key(key, mask, sizeof(mask));
  memset(key, 0, sizeof(key));
  if (!have) lv_label_set_text(camkey_cur, "Kein Key hinterlegt");
  else lv_label_set_text_fmt(camkey_cur, "%s: %s", own ? "Eigener Key" : "Standard-Key (Firmware)", mask);
  if (!ok) {
    lv_label_set_text(camkey_status, "Speichern fehlgeschlagen");
    return;
  }
  if (!have) {
    lv_label_set_text(camkey_status, "Kein Key: Kameras bleiben leer");
    return;
  }
  // Check the key with a lookup of the first camera.
  SettingsRequest r = {};
  r.kind = SettingsRequest::CamLookup;
  r.cam_id = cam_n > 0 ? cams[0].id : 1670606347u;
  key_testing = true;
  cam_busy = true;
  lv_label_set_text(camkey_status, "Gespeichert, pr\xC3\xBC" "fe Key ...");
  queue_settings(r);
}

// --- ÖV / Kalender / MeteoSchweiz / Wartung ---------------------------------

bool ui_take_transit(int* idx) {
  if (tr_inflight || scr_off) return false;
  uint32_t now = now_ms();
  int want = -1;
  if (nav == Nav::Transit && static_cast<int32_t>(now - tr_next_ms[tr_idx]) >= 0) want = tr_idx;
  else if (nav == Nav::Home && static_cast<int32_t>(now - tr_next_ms[0]) >= 0) want = 0;
  if (want < 0) return false;
  *idx = want;
  tr_inflight = true;
  return true;
}

void ui_transit_done(int idx, bool ok, const Board& b) {
  tr_inflight = false;
  if (idx < 0 || idx >= kStationN) return;
  if (ok) boards[idx] = b;
  else boards[idx].ok = false;
  uint32_t wait = !ok ? kTransitRetryMs : (nav == Nav::Transit && tr_idx == idx) ? kTransitViewMs : kTransitHomeMs;
  tr_next_ms[idx] = now_ms() + wait;
  if (nav == Nav::Transit) tr_refresh_view();
  if (idx == 0) home_chips_refresh();
}

void ui_station_search_done(bool ok, const Station* st, int n) {
  st_busy = false;
  st_hit_n = n < 0 ? 0 : (n > kMaxStationHits ? kMaxStationHits : n);
  memcpy(st_hits, st, sizeof(Station) * st_hit_n);
  if (nav != Nav::StationSearch) return;
  lv_label_set_text(st_status, !ok ? "Suche fehlgeschlagen" : st_hit_n == 0 ? "Keine Haltestelle gefunden" : "Haltestelle antippen");
  fill_st_list();
}

void ui_station_saved(bool ok, int idx) {
  st_busy = false;
  if (ok) {
    settings_load_stations(stations);
    boards[idx] = Board();
    tr_next_ms[idx] = now_ms();
  }
  if (nav == Nav::StationSearch) {
    if (ok) open_trset();
    else lv_label_set_text(st_status, "Speichern fehlgeschlagen");
  }
}

void ui_text_saved(bool ok, uint8_t which) {
  txt_busy = false;
  if (ok && (which == kUrlIcal || which == kUrlWaste)) cal_next_ms = now_ms();  // reload the calendar
  if (ok && which == kTextPlz) meteo_next_ms = now_ms();
  if (nav == Nav::TextEdit) {
    if (ok) txt_go_back();
    else lv_label_set_text(txt_status, "Speichern fehlgeschlagen");
  }
}

bool ui_take_calendar() {
  if (cal_inflight || cal == nullptr || !clock_synced()) return false;
  if (static_cast<int32_t>(now_ms() - cal_next_ms) < 0) return false;
  cal_inflight = true;
  return true;
}

void ui_calendar_done(bool ok, const Calendar& c) {
  cal_inflight = false;
  cal_next_ms = now_ms() + (ok ? kCalendarMs : kInfoRetryMs);
  if (ok || !cal_have) {
    memcpy(cal, &c, sizeof(Calendar));
    cal_have = true;
  }
  if (nav == Nav::Calendar) calv_refresh();
  if (nav == Nav::Home) home_chips_refresh();
  rem_tick(true);
}

bool ui_take_meteo() {
  if (meteo_inflight || !clock_synced()) return false;
  if (static_cast<int32_t>(now_ms() - meteo_next_ms) < 0) return false;
  meteo_inflight = true;
  return true;
}

void ui_meteo_done(bool ok, const MeteoNow& m) {
  meteo_inflight = false;
  meteo_next_ms = now_ms() + (ok ? kMeteoMs : kInfoRetryMs);
  if (ok) {
    meteo = m;
    meteo_have = true;
  }
  home_warn_refresh();
  if (nav == Nav::Rain) rain_refresh();
}

bool ui_take_ota(char* url, size_t n) {
  if (!ota_request) return false;
  ota_request = false;
  if (!settings_load_text(kUrlOta, url, n, nullptr)) {
    snprintf(ota_msg, sizeof(ota_msg), "Keine Update-URL");
    return false;
  }
  ota_running = true;
  return true;
}

void restart_now(lv_timer_t*) { esp_restart(); }

void ui_ota_done(bool ok, const char* err) {
  ota_running = false;
  snprintf(ota_msg, sizeof(ota_msg), "%s", ok ? "Installiert. Neustart ..." : err);
  if (nav == Nav::Maint) maint_refresh();
  if (ok) {
    lv_timer_t* t = lv_timer_create(restart_now, 1500, nullptr);
    lv_timer_set_repeat_count(t, 1);
  }
}
