#include "panel_ui.h"

#include <Arduino.h>
#include <lvgl.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "font_dejavu.h"
#include "panel_hw.h"
#include <esp_heap_caps.h>

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

// Nezz visionOS dark theme: warm black, translucent cards, Apple system colors.
constexpr uint32_t kBgTop = 0x3A3148;
constexpr uint32_t kBgBottom = 0x191816;
constexpr uint32_t kText = 0xFFFFFF;
constexpr uint32_t kTextDim = 0xD3D3D3;
constexpr uint32_t kGreen = 0x32D74B;
constexpr uint32_t kBlue = 0x0A84FF;
constexpr uint32_t kYellow = 0xFFD60A;
constexpr uint32_t kOrange = 0xFF9F0A;
constexpr uint32_t kCyan = 0x5AC8F5;
constexpr uint32_t kPurple = 0xBF5AF2;

enum Section : uint8_t { kSecFav = 0, kSecLight = 1, kSecShutter = 2, kSecOther = 3 };
enum class Nav : uint8_t { Home, Category, Weather };

const char* kShutterLabel[2] = {"Auf", "Ab"};
const char* kShutterCommand[2] = {"FullUp", "FullDown"};
const uint32_t kShutterColor[2] = {kBlue, kGreen};
const uint32_t kSectionAccent[4] = {kYellow, kOrange, kCyan, kPurple};

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
lv_obj_t* weather_deg = nullptr;
lv_obj_t* weather_cond = nullptr;
// Forecast panels inside the weather card: [0] Heute, [1] Morgen.
lv_obj_t* fc_icon[2] = {nullptr, nullptr};
// Weather day detail screen.
lv_obj_t* wx_root = nullptr;
lv_obj_t* wx_title = nullptr;
lv_obj_t* wx_page = nullptr;
lv_obj_t* wx_icon = nullptr;
lv_obj_t* wx_temp = nullptr;
lv_obj_t* wx_cond = nullptr;
lv_obj_t* wx_info[3] = {nullptr, nullptr, nullptr};
lv_obj_t* wx_hour[kWxSlots];
lv_obj_t* wx_sicon[kWxSlots];
lv_obj_t* wx_stemp[kWxSlots];
lv_obj_t* wx_rainbar[kWxSlots];
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
lv_obj_t* card_btn[4] = {};
lv_obj_t* card_sub[4] = {};
lv_obj_t* cat_title = nullptr;
lv_obj_t* cat_page = nullptr;
lv_obj_t* prev_hint = nullptr;
lv_obj_t* next_hint = nullptr;
lv_obj_t* cat_status = nullptr;
lv_obj_t* msg_status = nullptr;
lv_obj_t* tiles[kPerPage] = {};
lv_obj_t* tile_name[kPerPage] = {};
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

void style_glass(lv_obj_t* obj, lv_coord_t radius) {
  lv_obj_set_style_bg_color(obj, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(obj, 170, 0);
  lv_obj_set_style_bg_color(obj, lv_color_hex(0xFFFFFF), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(obj, LV_OPA_20, LV_STATE_PRESSED);
  lv_obj_set_style_radius(obj, radius, 0);
  lv_obj_set_style_border_width(obj, 1, 0);
  lv_obj_set_style_border_color(obj, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_opa(obj, LV_OPA_40, 0);
  lv_obj_set_style_shadow_width(obj, 0, 0);
  lv_obj_set_style_outline_width(obj, 0, 0);
  lv_obj_set_style_pad_all(obj, 0, 0);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
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
      return "Sonstiges";
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

// --- Drawn weather icons (60x48 container), by WMO weather code ----------
enum class Sky { None, Sun, Partly, Cloud, Rain, Snow, Thunder, Fog };

Sky sky_for(int code) {
  if (code < 0) return Sky::None;
  if (code == 0) return Sky::Sun;
  if (code <= 2) return Sky::Partly;
  if (code == 3) return Sky::Cloud;
  if (code == 45 || code == 48) return Sky::Fog;
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return Sky::Snow;
  if (code >= 95) return Sky::Thunder;
  if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return Sky::Rain;
  return Sky::Cloud;
}

int icon_pct = 100;  // scale for the drawn icons below
int ic(int v) { return v * icon_pct / 100; }

lv_obj_t* icon_blob(lv_obj_t* parent, int x, int y, int w, int h, uint32_t color, lv_opa_t opa) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_set_pos(o, ic(x), ic(y));
  lv_obj_set_size(o, ic(w) < 2 ? 2 : ic(w), ic(h) < 2 ? 2 : ic(h));
  lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(o, opa, 0);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_set_style_shadow_width(o, 0, 0);
  lv_obj_set_style_pad_all(o, 0, 0);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}

void icon_cloud(lv_obj_t* p, int dx, int dy, uint32_t color) {
  icon_blob(p, dx + 4, dy + 14, 48, 20, color, LV_OPA_COVER);
  icon_blob(p, dx + 10, dy + 6, 22, 22, color, LV_OPA_COVER);
  icon_blob(p, dx + 24, dy + 0, 26, 26, color, LV_OPA_COVER);
}

// Draws the icon for a WMO code into box (designed for 60x48 at 100 %).
void weather_icon(lv_obj_t* box, int code, int pct = 100) {
  icon_pct = pct;
  lv_obj_clean(box);
  Sky sky = sky_for(code);
  switch (sky) {
    case Sky::None:
      break;
    case Sky::Sun:
      icon_blob(box, 12, 6, 36, 36, kYellow, LV_OPA_30);
      icon_blob(box, 18, 12, 24, 24, kYellow, LV_OPA_COVER);
      break;
    case Sky::Partly:
      icon_blob(box, 26, 0, 30, 30, kYellow, LV_OPA_COVER);
      icon_cloud(box, 0, 12, 0xE5E5EA);
      break;
    case Sky::Cloud:
      icon_cloud(box, 4, 6, 0xC7C7CC);
      break;
    case Sky::Fog:
      for (int i = 0; i < 3; ++i) {
        icon_blob(box, 6 + (i % 2) * 6, 10 + i * 11, 44, 6, 0xC7C7CC, LV_OPA_COVER);
      }
      break;
    case Sky::Rain:
      icon_cloud(box, 4, 0, 0xAEAEB2);
      for (int i = 0; i < 3; ++i) {
        icon_blob(box, 14 + i * 12, 36, 4, 11, kCyan, LV_OPA_COVER);
      }
      break;
    case Sky::Snow:
      icon_cloud(box, 4, 0, 0xD1D1D6);
      for (int i = 0; i < 3; ++i) {
        icon_blob(box, 13 + i * 12, 38 + (i % 2) * 4, 7, 7, 0xFFFFFF, LV_OPA_COVER);
      }
      break;
    case Sky::Thunder: {
      icon_cloud(box, 4, 0, 0x8E8E93);
      // Bolt as two thin blobs (lv_line points would need per-icon storage).
      icon_blob(box, 26, 30, 5, 10, kYellow, LV_OPA_COVER);
      icon_blob(box, 22, 38, 5, 10, kYellow, LV_OPA_COVER);
      break;
    }
  }
  icon_pct = 100;
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

void style_tile(lv_obj_t* btn, LoxoneKind kind, bool is_known, bool on) {
  style_glass(btn, 24);
  if (kind == kLoxonePulse) {
    lv_obj_set_style_border_color(btn, lv_color_hex(kCyan), 0);
    lv_obj_set_style_border_opa(btn, LV_OPA_70, 0);
  } else if (is_known && on) {
    lv_obj_set_style_bg_color(btn, lv_color_hex(kGreen), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_40, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(kGreen), 0);
    lv_obj_set_style_border_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 2, 0);
  } else if (!is_known && kind != kLoxonePulse) {
    lv_obj_set_style_border_opa(btn, LV_OPA_80, 0);
  }
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
  style_tile(tiles[slot], kLoxoneSwitch, true, on);
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
  style_tile(tiles[slot], ctrl.kind, known[index], known_on[index]);
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

void fill_home_cards() {
  for (int s = 0; s < 4; ++s) {
    char text[32];
    if (s == kSecOther) {
      snprintf(text, sizeof(text), "Relais · %s", panel_relay_is_on() ? "An" : "Aus");
    } else {
      int count = 0;
      for (size_t i = 0; i < control_count; ++i) {
        if (section_of(controls[i]) == static_cast<Section>(s)) {
          count++;
        }
      }
      if (count == 0) {
        snprintf(text, sizeof(text), "Leer");
      } else {
        snprintf(text, sizeof(text), "%d", count);
      }
    }
    lv_label_set_text(card_sub[s], text);
  }
}

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
  lv_obj_set_style_text_color(cat_title, lv_color_hex(kSectionAccent[current_section]), 0);
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

void show_home() {
  nav = Nav::Home;
  page = -1;
  hide(cat_root);
  hide(wx_root);
  hide(message);
  show(home_root);
  fill_home_cards();
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

void swipe_page(int dir) {
  if (nav == Nav::Weather) {
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
  bool tap = touch_max_move <= kTapSlop && millis() - touch_t0 <= kTapMaxMs;
  gesture_consumed = !tap;
  intptr_t g = 0;
  if (adx >= kSwipe || ady >= kSwipe) {
    // Home is the only edge gesture: swipe up from the bottom edge. There is
    // no right-edge "back" any more: a left swipe that starts near the right
    // edge (where a thumb naturally starts) used to go home instead of
    // paging. Horizontal swipes page from any start position.
    if (gesture_y0 >= kScreen - kEdge && dy <= -kSwipe && ady > adx) {
      g = 1;
    } else if (nav == Nav::Weather && dy <= -kSwipe && ady > adx + 8) {
      g = 1;  // the day view has no vertical use, so any upward swipe goes home
    } else if (adx >= kSwipe && adx > ady + 8) {
      g = dx < 0 ? 3 : 4;
    }
  }
  Serial.printf("Touch %s from %d,%d to %d,%d dx=%d dy=%d move=%d %ums%s\n", tap ? "tap" : "gesture",
                static_cast<int>(gesture_x0), static_cast<int>(gesture_y0), static_cast<int>(touch_x),
                static_cast<int>(touch_y), dx, dy, touch_max_move,
                static_cast<unsigned>(millis() - touch_t0),
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
  lv_obj_clear_state(obj, LV_STATE_PRESSED);
  lv_timer_del(timer);
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
  Serial.printf("Activate tile slot %d control %d\n", slot, index);
  if (index == kLocalRelay) {
    panel_relay_set(!panel_relay_is_on());
    Serial.printf("Relay 1 %s\n", panel_relay_is_on() ? "on" : "off");
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
  Serial.printf("Activate shutter row %d %s\n", row_slot, kShutterCommand[which]);
  queue_command(current.rows[row_slot].a, kShutterCommand[which]);
  lv_label_set_text(shutter_pos[row_slot], "Sende...");
}

void on_category(lv_event_t* event) {
  if (gesture_consumed) {
    return;
  }
  int which = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
  if (which < kSecFav || which > kSecOther) {
    return;
  }
  Serial.printf("Activate category %d\n", which);
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
  show(wx_root);
  refresh_weather_day();
}

void on_day_panel(lv_event_t* event) {
  if (gesture_consumed) {
    return;
  }
  int day = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
  Serial.printf("Activate weather day %d\n", day);
  open_weather_day(day);
}

void on_retry(lv_event_t*) {
  if (gesture_consumed) {
    return;
  }
  retry_pending = true;
}

void style_shutter_button(lv_obj_t* btn, uint32_t color) {
  lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(btn, 190, 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0xFFFFFF), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(btn, LV_OPA_30, LV_STATE_PRESSED);
  lv_obj_set_style_border_width(btn, 0, 0);
  lv_obj_set_style_radius(btn, 20, 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  lv_obj_set_style_outline_width(btn, 0, 0);
  lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
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

void make_orb(lv_obj_t* parent, int x, int y, int d, uint32_t color, lv_opa_t opa) {
  lv_obj_t* orb = lv_obj_create(parent);
  lv_obj_set_pos(orb, x, y);
  lv_obj_set_size(orb, d, d);
  lv_obj_set_style_radius(orb, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(orb, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(orb, opa, 0);
  lv_obj_set_style_border_width(orb, 0, 0);
  lv_obj_set_style_shadow_width(orb, 0, 0);
  lv_obj_clear_flag(orb, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(orb, LV_OBJ_FLAG_SCROLLABLE);
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

lv_obj_t* wx_handle(lv_obj_t* parent, int x) {
  lv_obj_t* h = lv_obj_create(parent);
  lv_obj_set_pos(h, x, 190);
  lv_obj_set_size(h, 5, 80);
  lv_obj_set_style_radius(h, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(h, lv_color_hex(kText), 0);
  lv_obj_set_style_bg_opa(h, LV_OPA_30, 0);
  lv_obj_set_style_border_width(h, 0, 0);
  lv_obj_clear_flag(h, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(h, LV_OBJ_FLAG_SCROLLABLE);
  return h;
}

constexpr int kColW = 55;
constexpr int kCurveTop = 94;
constexpr int kCurveH = 44;
constexpr int kRainBase = 196;
constexpr int kRainH = 40;

void build_weather_screen(lv_obj_t* screen) {
  wx_root = lv_obj_create(screen);
  lv_obj_set_pos(wx_root, 0, 0);
  lv_obj_set_size(wx_root, kScreen, kScreen);
  style_plain(wx_root);
  lv_obj_clear_flag(wx_root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(wx_root, LV_OBJ_FLAG_CLICKABLE);

  wx_title = wx_label(wx_root, &font_dejavu_20, kCyan, 24, 16);
  wx_page = make_label(wx_root, &font_dejavu_14, lv_color_hex(kTextDim));
  lv_obj_align(wx_page, LV_ALIGN_TOP_RIGHT, -24, 22);

  lv_obj_t* head = lv_obj_create(wx_root);
  lv_obj_set_pos(head, kMargin, 50);
  lv_obj_set_size(head, kScreen - 2 * kMargin, 124);
  style_glass(head, 24);
  lv_obj_clear_flag(head, LV_OBJ_FLAG_CLICKABLE);
  wx_icon = lv_obj_create(head);
  style_plain(wx_icon);
  lv_obj_set_pos(wx_icon, 14, 14);
  lv_obj_set_size(wx_icon, 72, 58);
  lv_obj_clear_flag(wx_icon, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(wx_icon, LV_OBJ_FLAG_SCROLLABLE);
  wx_temp = wx_label(head, &lv_font_montserrat_28, kText, 100, 12);
  wx_cond = wx_label(head, &font_dejavu_14, kTextDim, 100, 50);
  wx_empty = wx_label(head, &font_dejavu_20, kTextDim, 100, 40);
  wx_info[0] = wx_label(head, &font_dejavu_14, kCyan, 16, 80);
  wx_info[1] = wx_label(head, &font_dejavu_14, kText, 16, 100);
  wx_info[2] = wx_label(head, &font_dejavu_14, kText, 236, 80);

  lv_obj_t* tl = lv_obj_create(wx_root);
  lv_obj_set_pos(tl, kMargin, 184);
  lv_obj_set_size(tl, kScreen - 2 * kMargin, 270);
  style_glass(tl, 24);
  lv_obj_clear_flag(tl, LV_OBJ_FLAG_CLICKABLE);

  wx_curve = lv_line_create(tl);
  lv_obj_set_pos(wx_curve, 0, 0);
  lv_obj_set_style_line_width(wx_curve, 3, 0);
  lv_obj_set_style_line_color(wx_curve, lv_color_hex(kOrange), 0);
  lv_obj_set_style_line_rounded(wx_curve, true, 0);

  for (int i = 0; i < kWxSlots; ++i) {
    int cx = i * kColW;
    wx_hour[i] = wx_label(tl, &font_dejavu_14, kTextDim, cx, 8);
    lv_obj_set_width(wx_hour[i], kColW);
    lv_obj_set_style_text_align(wx_hour[i], LV_TEXT_ALIGN_CENTER, 0);
    char h[4];
    snprintf(h, sizeof(h), "%02d", i * 3);
    lv_label_set_text(wx_hour[i], h);
    wx_sicon[i] = lv_obj_create(tl);
    style_plain(wx_sicon[i]);
    lv_obj_set_pos(wx_sicon[i], cx + 9, 30);
    lv_obj_set_size(wx_sicon[i], 38, 30);
    lv_obj_clear_flag(wx_sicon[i], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(wx_sicon[i], LV_OBJ_FLAG_SCROLLABLE);
    wx_stemp[i] = wx_label(tl, &font_dejavu_14, kText, cx, 66);
    lv_obj_set_width(wx_stemp[i], kColW);
    lv_obj_set_style_text_align(wx_stemp[i], LV_TEXT_ALIGN_CENTER, 0);
    wx_rainbar[i] = lv_obj_create(tl);
    lv_obj_set_style_bg_color(wx_rainbar[i], lv_color_hex(kBlue), 0);
    lv_obj_set_style_bg_opa(wx_rainbar[i], LV_OPA_80, 0);
    lv_obj_set_style_radius(wx_rainbar[i], 4, 0);
    lv_obj_set_style_border_width(wx_rainbar[i], 0, 0);
    lv_obj_clear_flag(wx_rainbar[i], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(wx_rainbar[i], LV_OBJ_FLAG_SCROLLABLE);
    wx_smm[i] = wx_label(tl, &font_dejavu_14, kCyan, cx, 200);
    lv_obj_set_width(wx_smm[i], kColW);
    lv_obj_set_style_text_align(wx_smm[i], LV_TEXT_ALIGN_CENTER, 0);
    wx_sprob[i] = wx_label(tl, &font_dejavu_14, kTextDim, cx, 220);
    lv_obj_set_width(wx_sprob[i], kColW);
    lv_obj_set_style_text_align(wx_sprob[i], LV_TEXT_ALIGN_CENTER, 0);
    wx_swind[i] = wx_label(tl, &font_dejavu_14, kTextDim, cx, 240);
    lv_obj_set_width(wx_swind[i], kColW);
    lv_obj_set_style_text_align(wx_swind[i], LV_TEXT_ALIGN_CENTER, 0);
  }

  wx_prev_hint = wx_handle(wx_root, 5);
  wx_next_hint = wx_handle(wx_root, 470);
  lv_obj_t* home_hint = lv_obj_create(wx_root);
  lv_obj_set_pos(home_hint, 200, 468);
  lv_obj_set_size(home_hint, 80, 5);
  lv_obj_set_style_radius(home_hint, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(home_hint, lv_color_hex(kText), 0);
  lv_obj_set_style_bg_opa(home_hint, LV_OPA_30, 0);
  lv_obj_set_style_border_width(home_hint, 0, 0);
  lv_obj_clear_flag(home_hint, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(home_hint, LV_OBJ_FLAG_SCROLLABLE);
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
  char buf[96];
  const char* rel = wx_day == 0 ? "Heute" : wx_day == 1 ? "Morgen" : nullptr;
  const char* wd = (m >= 1 && m <= 12) ? kWeekday[weekday_of(y, m, dd)] : "";
  if (rel != nullptr) {
    snprintf(buf, sizeof(buf), "%s \xC2\xB7 %s %d.%d.", rel, wd, dd, m);
  } else {
    snprintf(buf, sizeof(buf), "%s %d.%d.", wd, dd, m);
  }
  lv_label_set_text(wx_title, buf);
  snprintf(buf, sizeof(buf), "%d / %d", wx_day + 1, wx_count);
  lv_label_set_text(wx_page, buf);
  lv_obj_align(wx_page, LV_ALIGN_TOP_RIGHT, -24, 22);
  weather_icon(wx_icon, d.code, 115);
  snprintf(buf, sizeof(buf), "%d\xC2\xB0 / %d\xC2\xB0", round_c(d.t_max), round_c(d.t_min));
  lv_label_set_text(wx_temp, buf);
  lv_label_set_text(wx_cond, weather_name(d.code));
  snprintf(buf, sizeof(buf), "Niederschlag %.1f mm \xC2\xB7 %d %%", static_cast<double>(d.precip), d.prob_max);
  lv_label_set_text(wx_info[0], buf);
  snprintf(buf, sizeof(buf), "Wind max %d km/h", round_c(d.wind_max));
  lv_label_set_text(wx_info[1], buf);
  // Plain hyphen: the en dash is outside the font range.
  snprintf(buf, sizeof(buf), "Sonne %s - %s", d.sunrise, d.sunset);
  lv_label_set_text(wx_info[2], buf);

  float lo = d.slot[0].temp, hi = d.slot[0].temp, rmax = 2.0f;
  for (int i = 0; i < kWxSlots; ++i) {
    if (d.slot[i].temp < lo) lo = d.slot[i].temp;
    if (d.slot[i].temp > hi) hi = d.slot[i].temp;
    if (d.slot[i].precip > rmax) rmax = d.slot[i].precip;
  }
  float span = hi - lo < 1.f ? 1.f : hi - lo;
  for (int i = 0; i < kWxSlots; ++i) {
    const WeatherSlot& s = d.slot[i];
    weather_icon(wx_sicon[i], s.code, 62);
    snprintf(buf, sizeof(buf), "%d\xC2\xB0", round_c(s.temp));
    lv_label_set_text(wx_stemp[i], buf);
    wx_curve_pts[i].x = i * kColW + kColW / 2;
    wx_curve_pts[i].y = kCurveTop + kCurveH - static_cast<int>((s.temp - lo) / span * kCurveH);
    int h = static_cast<int>(s.precip / rmax * kRainH + 0.5f);
    if (s.precip > 0.f && h < 2) h = 2;
    if (h <= 0) {
      hide(wx_rainbar[i]);
    } else {
      show(wx_rainbar[i]);
      lv_obj_set_pos(wx_rainbar[i], i * kColW + 17, kRainBase - h);
      lv_obj_set_size(wx_rainbar[i], 20, h);
    }
    snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(s.precip));
    lv_label_set_text(wx_smm[i], buf);
    snprintf(buf, sizeof(buf), "%d%%", s.prob);
    lv_label_set_text(wx_sprob[i], buf);
    snprintf(buf, sizeof(buf), "%d", round_c(s.wind));
    lv_label_set_text(wx_swind[i], buf);
  }
  lv_line_set_points(wx_curve, wx_curve_pts, kWxSlots);
  if (wx_day > 0) show(wx_prev_hint); else hide(wx_prev_hint);
  if (wx_day + 1 < wx_count) show(wx_next_hint); else hide(wx_next_hint);
}

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
  if (message_open) {
    snprintf(out, n, "message");
  } else if (nav == Nav::Home) {
    snprintf(out, n, "home");
  } else if (nav == Nav::Weather) {
    snprintf(out, n, "weather %d/%d", wx_day + 1, wx_count);
  } else {
    snprintf(out, n, "category %s %d/%d", section_name(current_section), section_page + 1,
             section_pages[current_section]);
  }
}

void ui_touch_sample(bool pressed, int x, int y) {
  if (pressed) {
    if (!touch_down) {
      touch_down = true;
      touch_t0 = millis();
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

  make_orb(screen, 250, -80, 280, kPurple, LV_OPA_30);
  make_orb(screen, -90, 300, 240, kCyan, LV_OPA_20);
  make_orb(screen, 300, 360, 180, kOrange, LV_OPA_10);

  home_root = lv_obj_create(screen);
  lv_obj_set_pos(home_root, 0, 0);
  lv_obj_set_size(home_root, kScreen, kScreen);
  style_plain(home_root);

  lv_obj_t* weather = lv_obj_create(home_root);
  lv_obj_set_pos(weather, kMargin, 16);
  lv_obj_set_size(weather, kScreen - 2 * kMargin, 184);
  style_glass(weather, 28);
  lv_obj_clear_flag(weather, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t* place = make_label(weather, &font_dejavu_20, lv_color_hex(kTextDim));
  lv_obj_set_pos(place, 22, 18);
  lv_label_set_text(place, "Buchs");

  weather_temp = make_label(weather, &lv_font_montserrat_28, lv_color_hex(kText));
  lv_obj_set_pos(weather_temp, 22, 58);
  lv_label_set_text(weather_temp, "--");

  weather_deg = make_label(weather, &font_dejavu_20, lv_color_hex(kText));
  lv_label_set_text(weather_deg, "°");
  lv_obj_align_to(weather_deg, weather_temp, LV_ALIGN_OUT_RIGHT_TOP, 4, 2);

  weather_cond = make_label(weather, &font_dejavu_14, lv_color_hex(kTextDim));
  lv_obj_set_pos(weather_cond, 22, 108);
  lv_obj_set_width(weather_cond, 128);
  lv_label_set_long_mode(weather_cond, LV_LABEL_LONG_WRAP);
  lv_label_set_text(weather_cond, "Wetter wird geladen");

  // Today / tomorrow panels on the right of the card.
  static const char* kDayName[2] = {"Heute", "Morgen"};
  for (int d = 0; d < 2; ++d) {
    lv_obj_t* panel = lv_obj_create(weather);
    lv_obj_set_pos(panel, 160 + d * 136, 14);
    lv_obj_set_size(panel, 128, 154);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_10, 0);
    lv_obj_set_style_radius(panel, 20, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_set_style_shadow_width(panel, 0, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(panel, LV_OPA_30, LV_STATE_PRESSED);
    lv_obj_add_event_cb(panel, on_day_panel, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<intptr_t>(d)));
    watch(panel);

    lv_obj_t* name = make_label(panel, &font_dejavu_14, lv_color_hex(kTextDim));
    lv_label_set_text(name, kDayName[d]);
    lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 8);

    fc_icon[d] = lv_obj_create(panel);
    style_plain(fc_icon[d]);
    lv_obj_set_size(fc_icon[d], 60, 48);
    lv_obj_align(fc_icon[d], LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_clear_flag(fc_icon[d], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(fc_icon[d], LV_OBJ_FLAG_SCROLLABLE);

    fc_temp[d] = make_label(panel, &font_dejavu_20, lv_color_hex(kText));
    lv_label_set_text(fc_temp[d], "--");
    lv_obj_align(fc_temp[d], LV_ALIGN_TOP_MID, 0, 88);

    fc_rain[d] = make_label(panel, &font_dejavu_14, lv_color_hex(kCyan));
    lv_label_set_text(fc_rain[d], "");
    lv_obj_align(fc_rain[d], LV_ALIGN_TOP_MID, 0, 122);
  }

  const int card_x[2] = {20, 246};
  const int card_y[2] = {214, 340};
  for (int i = 0; i < 4; ++i) {
    int col = i % 2;
    int row = i / 2;
    lv_obj_t* btn = lv_btn_create(home_root);
    lv_obj_set_pos(btn, card_x[col], card_y[row]);
    lv_obj_set_size(btn, 214, 112);
    style_glass(btn, 24);
    lv_obj_add_event_cb(btn, on_category, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<intptr_t>(i)));
    watch(btn);
    card_btn[i] = btn;

    lv_obj_t* dot = lv_obj_create(btn);
    lv_obj_set_pos(dot, 18, 22);
    lv_obj_set_size(dot, 14, 14);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(kSectionAccent[i]), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* name = make_label(btn, &font_dejavu_20, lv_color_hex(kText));
    lv_obj_set_pos(name, 44, 16);
    lv_label_set_text(name, section_name(static_cast<Section>(i)));

    card_sub[i] = make_label(btn, &font_dejavu_14, lv_color_hex(kTextDim));
    lv_obj_set_pos(card_sub[i], 18, 64);
    lv_label_set_text(card_sub[i], "");
  }

  cat_root = lv_obj_create(screen);
  lv_obj_set_pos(cat_root, 0, 0);
  lv_obj_set_size(cat_root, kScreen, kScreen);
  style_plain(cat_root);

  cat_title = make_label(cat_root, &font_dejavu_20, lv_color_hex(kText));
  lv_obj_set_pos(cat_title, 24, 16);
  lv_label_set_text(cat_title, "");

  cat_page = make_label(cat_root, &font_dejavu_14, lv_color_hex(kTextDim));
  lv_obj_align(cat_page, LV_ALIGN_TOP_RIGHT, -24, 22);
  hide(cat_page);

  cat_status = make_label(cat_root, &font_dejavu_14, lv_color_hex(kTextDim));
  lv_obj_set_pos(cat_status, 24, 48);
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
    style_glass(btn, 24);
    lv_obj_add_event_cb(btn, on_tile, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<intptr_t>(slot)));
    watch(btn);
    tiles[slot] = btn;

    tile_name[slot] = make_label(btn, &font_dejavu_20, lv_color_hex(kText));
    lv_obj_set_width(tile_name[slot], kTileW - 28);
    lv_label_set_long_mode(tile_name[slot], LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(tile_name[slot], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(tile_name[slot], LV_ALIGN_CENTER, 0, -18);

    tile_sub[slot] = make_label(btn, &font_dejavu_14, lv_color_hex(kTextDim));
    lv_obj_set_width(tile_sub[slot], kTileW - 28);
    lv_label_set_long_mode(tile_sub[slot], LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(tile_sub[slot], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(tile_sub[slot], LV_ALIGN_CENTER, 0, 28);
    hide(btn);
  }

  for (int row_slot = 0; row_slot < kRowsPerPage; ++row_slot) {
    lv_obj_t* box = lv_obj_create(cat_root);
    lv_obj_set_pos(box, kMargin, kContentY + row_slot * (kTileH + kGap));
    lv_obj_set_size(box, kScreen - 2 * kMargin, kTileH);
    style_glass(box, 24);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_CLICKABLE);
    shutter_box[row_slot] = box;

    shutter_title[row_slot] = make_label(box, &font_dejavu_20, lv_color_hex(kText));
    lv_obj_set_pos(shutter_title[row_slot], 16, 12);
    lv_obj_set_width(shutter_title[row_slot], 300);
    lv_label_set_long_mode(shutter_title[row_slot], LV_LABEL_LONG_DOT);

    shutter_pos[row_slot] = make_label(box, &font_dejavu_14, lv_color_hex(kTextDim));
    lv_obj_align(shutter_pos[row_slot], LV_ALIGN_TOP_RIGHT, -16, 16);

    for (int which = 0; which < 2; ++which) {
      lv_obj_t* btn = lv_btn_create(box);
      lv_obj_set_pos(btn, 16 + which * 206, 56);
      lv_obj_set_size(btn, 196, 84);
      style_shutter_button(btn, kShutterColor[which]);
      intptr_t tag = static_cast<intptr_t>(row_slot * 2 + which);
      lv_obj_add_event_cb(btn, on_shutter, LV_EVENT_CLICKED, reinterpret_cast<void*>(tag));
      watch(btn);
      lv_obj_t* label = make_label(btn, &font_dejavu_20, lv_color_hex(kText));
      lv_label_set_text(label, kShutterLabel[which]);
      lv_obj_center(label);
    }
    hide(box);
  }

  empty_hint = make_label(cat_root, &font_dejavu_20, lv_color_hex(kTextDim));
  lv_obj_set_width(empty_hint, 400);
  lv_label_set_long_mode(empty_hint, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(empty_hint, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(empty_hint, LV_ALIGN_CENTER, 0, 20);
  hide(empty_hint);

  // Right-edge handle: shown by refresh_category() only when a next page
  // exists. (It used to mean "swipe back to home" and was always shown.)
  next_hint = lv_obj_create(cat_root);
  lv_obj_t* back_hint = next_hint;
  lv_obj_set_pos(back_hint, 470, 190);
  lv_obj_set_size(back_hint, 5, 80);
  lv_obj_set_style_radius(back_hint, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(back_hint, lv_color_hex(kText), 0);
  lv_obj_set_style_bg_opa(back_hint, LV_OPA_30, 0);
  lv_obj_set_style_border_width(back_hint, 0, 0);
  lv_obj_clear_flag(back_hint, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(back_hint, LV_OBJ_FLAG_SCROLLABLE);
  hide(back_hint);

  // Mirror of back_hint on the left edge, shown by refresh_category() only
  // when there is a previous page.
  prev_hint = lv_obj_create(cat_root);
  lv_obj_set_pos(prev_hint, 5, 190);
  lv_obj_set_size(prev_hint, 5, 80);
  lv_obj_set_style_radius(prev_hint, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(prev_hint, lv_color_hex(kText), 0);
  lv_obj_set_style_bg_opa(prev_hint, LV_OPA_30, 0);
  lv_obj_set_style_border_width(prev_hint, 0, 0);
  lv_obj_clear_flag(prev_hint, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(prev_hint, LV_OBJ_FLAG_SCROLLABLE);
  hide(prev_hint);

  lv_obj_t* home_hint = lv_obj_create(cat_root);
  lv_obj_set_pos(home_hint, 200, 468);
  lv_obj_set_size(home_hint, 80, 5);
  lv_obj_set_style_radius(home_hint, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(home_hint, lv_color_hex(kText), 0);
  lv_obj_set_style_bg_opa(home_hint, LV_OPA_30, 0);
  lv_obj_set_style_border_width(home_hint, 0, 0);
  lv_obj_clear_flag(home_hint, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(home_hint, LV_OBJ_FLAG_SCROLLABLE);

  hide(cat_root);

  build_weather_screen(screen);

  msg_status = make_label(screen, &font_dejavu_14, lv_color_hex(kTextDim));
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

  message_body = make_label(message, &font_dejavu_20, lv_color_hex(kText));
  lv_obj_set_width(message_body, 392);
  lv_label_set_long_mode(message_body, LV_LABEL_LONG_WRAP);
  lv_obj_align(message_body, LV_ALIGN_TOP_LEFT, 20, 80);

  retry_btn = lv_btn_create(message);
  lv_obj_set_size(retry_btn, 392, 80);
  lv_obj_align(retry_btn, LV_ALIGN_BOTTOM_MID, 0, -16);
  style_glass(retry_btn, 20);
  lv_obj_set_style_bg_color(retry_btn, lv_color_hex(kGreen), 0);
  lv_obj_set_style_bg_opa(retry_btn, LV_OPA_50, 0);
  lv_obj_add_event_cb(retry_btn, on_retry, LV_EVENT_CLICKED, nullptr);
  watch(retry_btn);
  lv_obj_t* retry_label = make_label(retry_btn, &font_dejavu_20, lv_color_hex(kText));
  lv_label_set_text(retry_label, "Try again");
  lv_obj_center(retry_label);
  hide(message);
  hide(home_root);
}

void ui_show_message(const char* title, const char* body, bool retry) {
  lv_label_set_text(message_title, title != nullptr ? title : "");
  lv_label_set_text(message_body, body != nullptr ? body : "");
  if (retry) {
    show(retry_btn);
  } else {
    hide(retry_btn);
  }
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
  Serial.printf("Home categories ready, Sonstiges pages %d\n", section_pages[kSecOther]);
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
  for (int d = 0; d < 2; ++d) {
    if (d >= count) {
      break;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%d\xC2\xB0 / %d\xC2\xB0", round_c(days[d].t_max), round_c(days[d].t_min));
    lv_label_set_text(fc_temp[d], buf);
    lv_obj_align(fc_temp[d], LV_ALIGN_TOP_MID, 0, 88);
    snprintf(buf, sizeof(buf), "%.1f mm", static_cast<double>(days[d].precip));
    lv_label_set_text(fc_rain[d], buf);
    lv_obj_align(fc_rain[d], LV_ALIGN_TOP_MID, 0, 122);
    weather_icon(fc_icon[d], days[d].code);
  }
}

void ui_set_weather(bool ok, int temp_c, int code) {
  if (weather_temp == nullptr || weather_cond == nullptr) {
    return;
  }
  if (!ok) {
    lv_label_set_text(weather_temp, "--");
    lv_label_set_text(weather_cond, "Wetter nicht verfügbar");
  } else {
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", temp_c);
    lv_label_set_text(weather_temp, buf);
    lv_label_set_text(weather_cond, weather_name(code));
  }
  if (weather_deg != nullptr) {
    lv_obj_align_to(weather_deg, weather_temp, LV_ALIGN_OUT_RIGHT_TOP, 4, 2);
  }
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
      reread_after[index] = millis() + kShutterRereadMs;
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
  uint32_t now = millis();
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
