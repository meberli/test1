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

namespace {

constexpr int kScreen = 480;
constexpr int kHeaderH = 80;
constexpr int kFooterY = 392;
constexpr int kMargin = 12;
constexpr int kTileW = 222;
constexpr int kTileH = 140;
constexpr int kRowsPerPage = 2;
constexpr int kPerPage = 4;
constexpr int kMaxPages = 32;
constexpr uint32_t kShutterRereadMs = 3000;
// Page-plan slot for the local GPIO40 relay. Not a Loxone control index.
constexpr int kLocalRelay = -2;

enum Section : uint8_t { kSecFav = 0, kSecLight = 1, kSecShutter = 2, kSecOther = 3 };

const char* kShutterLabel[2] = {"Auf", "Ab"};
const char* kShutterCommand[2] = {"FullUp", "FullDown"};
const uint32_t kShutterColor[2] = {0x245A8D, 0x1F6B45};

lv_obj_t* heading = nullptr;
lv_obj_t* status = nullptr;
lv_obj_t* section_btn[4] = {};
lv_obj_t* section_lbl[4] = {};
lv_obj_t* empty_hint = nullptr;
lv_obj_t* tiles[kPerPage] = {};
lv_obj_t* tile_name[kPerPage] = {};
lv_obj_t* tile_sub[kPerPage] = {};
lv_obj_t* shutter_box[kRowsPerPage] = {};
lv_obj_t* shutter_title[kRowsPerPage] = {};
lv_obj_t* shutter_pos[kRowsPerPage] = {};
lv_obj_t* prev_btn = nullptr;
lv_obj_t* next_btn = nullptr;
lv_obj_t* page_label = nullptr;
lv_obj_t* message = nullptr;
lv_obj_t* message_title = nullptr;
lv_obj_t* message_body = nullptr;
lv_obj_t* retry_btn = nullptr;

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

LoxoneControl controls[kLoxoneControlCap];
size_t control_count = 0;
char server_name[48] = "";
PagePlan pages[kMaxPages];
int page_n = 0;
int page = 0;
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

lv_obj_t* make_label(lv_obj_t* parent, const lv_font_t* font, lv_color_t color) {
  lv_obj_t* label = lv_label_create(parent);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, color, 0);
  return label;
}

void hide(lv_obj_t* obj) { lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN); }

void show(lv_obj_t* obj) { lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN); }

int row_y(int row_slot) { return kHeaderH + kMargin + row_slot * (kTileH + kMargin); }

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

uint32_t section_color(Section section) {
  switch (section) {
    case kSecFav:
      return 0xF4C15D;
    case kSecShutter:
      return 0x9EC7E8;
    case kSecOther:
      return 0xC5CED6;
    default:
      return 0xF2F5F7;
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

int page_count() { return page_n > 0 ? page_n : 1; }

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
  int i = 0;
  while (i < n) {
    if (current->nrows == kRowsPerPage) {
      if (page_n >= kMaxPages) {
        return;
      }
      current = &pages[page_n++];
      current->section = section;
      current->nrows = 0;
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

// Sonstiges always starts with the local relay, even when Loxone has no
// pushbuttons. That section used to be omitted, so the switch never appeared.
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
}

int first_content_page() {
  for (int i = 0; i < page_n; ++i) {
    if (pages[i].nrows > 0) {
      return i;
    }
  }
  return 0;
}

int first_page_of(Section section) {
  for (int i = 0; i < page_n; ++i) {
    if (pages[i].section == section) {
      return i;
    }
  }
  return 0;
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
  lv_color_t bg = lv_color_hex(0x243044);
  lv_coord_t border = 0;
  if (kind == kLoxonePulse) {
    bg = lv_color_hex(0x1B3A4B);
  } else if (is_known && on) {
    bg = lv_color_hex(0x2F7D32);
  } else if (is_known && !on) {
    bg = lv_color_hex(0x1A2330);
  } else {
    border = 2;
  }
  lv_obj_set_style_bg_color(btn, bg, 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x3E6B45), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn, 18, 0);
  lv_obj_set_style_border_width(btn, border, 0);
  lv_obj_set_style_border_color(btn, lv_color_hex(0x7FA3C4), 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  lv_obj_set_style_pad_all(btn, 8, 0);
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
    snprintf(out, n, "%s - %s", ctrl.room, mark);
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
  if (ctrl.room[0] != '\0') {
    snprintf(out, n, "%s - %s", ctrl.room, ctrl.name);
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

void style_section_tab(int index, bool active) {
  lv_obj_t* btn = section_btn[index];
  lv_color_t bg = lv_color_hex(active ? section_color(static_cast<Section>(index)) : 0x1A222B);
  lv_color_t fg = lv_color_hex(active ? 0x14202A : 0xD5DDE4);
  lv_obj_set_style_bg_color(btn, bg, 0);
  lv_obj_set_style_bg_color(btn, bg, LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(btn, 12, 0);
  lv_obj_set_style_border_width(btn, active ? 0 : 1, 0);
  lv_obj_set_style_border_color(btn, lv_color_hex(0x3A4654), 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  lv_obj_set_style_pad_all(btn, 0, 0);
  lv_obj_set_style_text_color(section_lbl[index], fg, 0);
}

void refresh_tiles() {
  int pages_n = page_count();
  if (page >= pages_n) {
    page = pages_n - 1;
  }
  if (page < 0) {
    page = 0;
  }
  for (int slot = 0; slot < kPerPage; ++slot) {
    hide(tiles[slot]);
  }
  for (int row_slot = 0; row_slot < kRowsPerPage; ++row_slot) {
    hide(shutter_box[row_slot]);
  }
  hide(heading);
  Section current_section = kSecFav;
  if (page_n > 0 && page < page_n) {
    const PagePlan& current = pages[page];
    current_section = current.section;
    lv_label_set_text(heading, section_name(current.section));
    lv_obj_set_style_text_color(heading, lv_color_hex(section_color(current.section)), 0);
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
    if (current.nrows == 0) {
      lv_label_set_text(empty_hint, empty_text(current.section));
      show(empty_hint);
    } else {
      hide(empty_hint);
    }
  }
  for (int i = 0; i < 4; ++i) {
    style_section_tab(i, static_cast<int>(current_section) == i);
    show(section_btn[i]);
  }

  if (pages_n <= 1) {
    hide(prev_btn);
    hide(next_btn);
    hide(page_label);
    return;
  }
  show(prev_btn);
  show(next_btn);
  show(page_label);
  char text[16];
  snprintf(text, sizeof(text), "%d / %d", page + 1, pages_n);
  lv_label_set_text(page_label, text);
  if (page == 0) {
    lv_obj_add_state(prev_btn, LV_STATE_DISABLED);
  } else {
    lv_obj_clear_state(prev_btn, LV_STATE_DISABLED);
  }
  if (page + 1 >= pages_n) {
    lv_obj_add_state(next_btn, LV_STATE_DISABLED);
  } else {
    lv_obj_clear_state(next_btn, LV_STATE_DISABLED);
  }
}

void set_grid_visible(bool visible) {
  if (!visible) {
    for (int i = 0; i < kPerPage; ++i) {
      hide(tiles[i]);
    }
    for (int i = 0; i < kRowsPerPage; ++i) {
      hide(shutter_box[i]);
    }
    hide(prev_btn);
    hide(next_btn);
    hide(page_label);
    hide(empty_hint);
    for (int i = 0; i < 4; ++i) {
      hide(section_btn[i]);
    }
    show(heading);
    return;
  }
  refresh_tiles();
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
  int slot = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
  int index = control_for_slot(slot);
  if (index == kLocalRelay) {
    panel_relay_set(!panel_relay_is_on());
    refresh_tiles();
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
  if (command_pending) {
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
  int index = current.rows[row_slot].a;
  queue_command(index, kShutterCommand[which]);
  lv_label_set_text(shutter_pos[row_slot], "Sende...");
}

void on_section(lv_event_t* event) {
  int which = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
  if (which < kSecFav || which > kSecOther) {
    return;
  }
  int target = first_page_of(static_cast<Section>(which));
  if (page == target) {
    return;
  }
  page = target;
  arm_visible_reads();
  refresh_tiles();
  publish_status();
}

void on_prev(lv_event_t*) {
  if (page > 0) {
    page--;
    arm_visible_reads();
    refresh_tiles();
    publish_status();
  }
}

void on_next(lv_event_t*) {
  if (page + 1 < page_count()) {
    page++;
    arm_visible_reads();
    refresh_tiles();
    publish_status();
  }
}

void on_retry(lv_event_t*) { retry_pending = true; }

void style_chrome_button(lv_obj_t* btn) {
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x2F6F4E), 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x3E8A62), LV_STATE_PRESSED);
  lv_obj_set_style_text_color(btn, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_radius(btn, 16, 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  lv_obj_set_style_border_width(btn, 0, 0);
}

void style_shutter_button(lv_obj_t* btn, uint32_t color) {
  lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(color), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(btn, 0, 0);
  lv_obj_set_style_border_width(btn, 3, LV_STATE_PRESSED);
  lv_obj_set_style_border_color(btn, lv_color_hex(0xFFFFFF), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn, 16, 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
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

}  // namespace

void ui_init() {
  lv_obj_t* screen = lv_scr_act();
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x101418), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

  heading = make_label(screen, &font_dejavu_20, lv_color_hex(0xF2F5F7));
  lv_obj_set_pos(heading, 16, 12);
  lv_label_set_text(heading, "Loxone");

  status = make_label(screen, &font_dejavu_14, lv_color_hex(0xA8B3BD));
  lv_obj_set_pos(status, 16, 44);
  lv_obj_set_width(status, 448);
  lv_label_set_long_mode(status, LV_LABEL_LONG_DOT);
  lv_label_set_text(status, "");

  for (int i = 0; i < 4; ++i) {
    lv_obj_t* btn = lv_btn_create(screen);
    lv_obj_set_pos(btn, 8 + i * 118, 6);
    lv_obj_set_size(btn, 110, 34);
    lv_obj_add_event_cb(btn, on_section, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<intptr_t>(i)));
    section_btn[i] = btn;
    section_lbl[i] = make_label(btn, &font_dejavu_14, lv_color_hex(0xD5DDE4));
    lv_label_set_text(section_lbl[i], section_name(static_cast<Section>(i)));
    lv_obj_set_width(section_lbl[i], 102);
    lv_label_set_long_mode(section_lbl[i], LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(section_lbl[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(section_lbl[i]);
    style_section_tab(i, false);
    hide(btn);
  }

  for (int slot = 0; slot < kPerPage; ++slot) {
    int col = slot % 2;
    int row = slot / 2;
    int x = kMargin + col * (kTileW + kMargin);
    int y = row_y(row);
    lv_obj_t* btn = lv_btn_create(screen);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, kTileW, kTileH);
    lv_obj_add_event_cb(btn, on_tile, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<intptr_t>(slot)));
    tiles[slot] = btn;

    tile_name[slot] = make_label(btn, &font_dejavu_20, lv_color_hex(0xFFFFFF));
    lv_obj_set_width(tile_name[slot], kTileW - 24);
    lv_label_set_long_mode(tile_name[slot], LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(tile_name[slot], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(tile_name[slot], LV_ALIGN_CENTER, 0, -16);

    tile_sub[slot] = make_label(btn, &font_dejavu_14, lv_color_hex(0xD5DDE4));
    lv_obj_set_width(tile_sub[slot], kTileW - 24);
    lv_label_set_long_mode(tile_sub[slot], LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(tile_sub[slot], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(tile_sub[slot], LV_ALIGN_CENTER, 0, 28);
    hide(btn);
  }

  for (int row_slot = 0; row_slot < kRowsPerPage; ++row_slot) {
    lv_obj_t* box = lv_obj_create(screen);
    lv_obj_set_pos(box, kMargin, row_y(row_slot));
    lv_obj_set_size(box, kScreen - 2 * kMargin, kTileH);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x1A222B), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_radius(box, 18, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_set_style_shadow_width(box, 0, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_CLICKABLE);
    shutter_box[row_slot] = box;

    shutter_title[row_slot] = make_label(box, &font_dejavu_20, lv_color_hex(0xFFFFFF));
    lv_obj_set_pos(shutter_title[row_slot], 12, 8);
    lv_obj_set_width(shutter_title[row_slot], 320);
    lv_label_set_long_mode(shutter_title[row_slot], LV_LABEL_LONG_DOT);

    shutter_pos[row_slot] = make_label(box, &font_dejavu_14, lv_color_hex(0xD5DDE4));
    lv_obj_align(shutter_pos[row_slot], LV_ALIGN_TOP_RIGHT, -12, 12);

    for (int which = 0; which < 2; ++which) {
      lv_obj_t* btn = lv_btn_create(box);
      lv_obj_set_pos(btn, 12 + which * 222, 46);
      lv_obj_set_size(btn, 210, 82);
      style_shutter_button(btn, kShutterColor[which]);
      intptr_t tag = static_cast<intptr_t>(row_slot * 2 + which);
      lv_obj_add_event_cb(btn, on_shutter, LV_EVENT_CLICKED, reinterpret_cast<void*>(tag));
      lv_obj_t* label = make_label(btn, &font_dejavu_20, lv_color_hex(0xFFFFFF));
      lv_label_set_text(label, kShutterLabel[which]);
      lv_obj_center(label);
    }
    hide(box);
  }

  prev_btn = lv_btn_create(screen);
  lv_obj_set_pos(prev_btn, 12, kFooterY);
  lv_obj_set_size(prev_btn, 140, 72);
  style_chrome_button(prev_btn);
  lv_obj_add_event_cb(prev_btn, on_prev, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* prev_label = make_label(prev_btn, &font_dejavu_20, lv_color_hex(0xFFFFFF));
  lv_label_set_text(prev_label, "Zurück");
  lv_obj_center(prev_label);

  next_btn = lv_btn_create(screen);
  lv_obj_set_pos(next_btn, 328, kFooterY);
  lv_obj_set_size(next_btn, 140, 72);
  style_chrome_button(next_btn);
  lv_obj_add_event_cb(next_btn, on_next, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* next_label = make_label(next_btn, &font_dejavu_20, lv_color_hex(0xFFFFFF));
  lv_label_set_text(next_label, "Weiter");
  lv_obj_center(next_label);

  page_label = make_label(screen, &font_dejavu_14, lv_color_hex(0xC5CED6));
  lv_obj_align(page_label, LV_ALIGN_BOTTOM_MID, 0, -28);
  hide(prev_btn);
  hide(next_btn);
  hide(page_label);

  empty_hint = make_label(screen, &font_dejavu_20, lv_color_hex(0xA8B3BD));
  lv_obj_set_width(empty_hint, 440);
  lv_label_set_long_mode(empty_hint, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(empty_hint, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(empty_hint, LV_ALIGN_CENTER, 0, -20);
  hide(empty_hint);

  message = lv_obj_create(screen);
  lv_obj_set_pos(message, 16, 88);
  lv_obj_set_size(message, 448, 376);
  lv_obj_set_style_bg_color(message, lv_color_hex(0x1A222B), 0);
  lv_obj_set_style_bg_opa(message, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(message, 0, 0);
  lv_obj_set_style_radius(message, 20, 0);
  lv_obj_set_style_pad_all(message, 16, 0);
  lv_obj_clear_flag(message, LV_OBJ_FLAG_SCROLLABLE);

  message_title = make_label(message, &lv_font_montserrat_28, lv_color_hex(0xF4C15D));
  lv_obj_set_width(message_title, 400);
  lv_label_set_long_mode(message_title, LV_LABEL_LONG_WRAP);
  lv_obj_align(message_title, LV_ALIGN_TOP_LEFT, 8, 8);

  message_body = make_label(message, &font_dejavu_20, lv_color_hex(0xE6EDF2));
  lv_obj_set_width(message_body, 400);
  lv_label_set_long_mode(message_body, LV_LABEL_LONG_WRAP);
  lv_obj_align(message_body, LV_ALIGN_TOP_LEFT, 8, 72);

  retry_btn = lv_btn_create(message);
  lv_obj_set_size(retry_btn, 400, 88);
  lv_obj_align(retry_btn, LV_ALIGN_BOTTOM_MID, 0, -8);
  style_chrome_button(retry_btn);
  lv_obj_add_event_cb(retry_btn, on_retry, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* retry_label = make_label(retry_btn, &font_dejavu_20, lv_color_hex(0xFFFFFF));
  lv_label_set_text(retry_label, "Try again");
  lv_obj_center(retry_label);
  hide(message);
}

void ui_show_message(const char* title, const char* body, bool retry) {
  lv_label_set_text(heading, "Loxone");
  lv_obj_set_style_text_color(heading, lv_color_hex(0xF2F5F7), 0);
  lv_label_set_text(message_title, title != nullptr ? title : "");
  lv_label_set_text(message_body, body != nullptr ? body : "");
  if (retry) {
    show(retry_btn);
  } else {
    hide(retry_btn);
  }
  show(message);
  set_grid_visible(false);
}

void ui_show_controls(const LoxoneControl* items, size_t count, size_t supported, const char* server) {
  if (count > kLoxoneControlCap) {
    count = kLoxoneControlCap;
  }
  control_count = count;
  page = 0;
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
  page = first_content_page();
  Serial.printf("UI screens %d, Sonstiges starts at %d\n", page_n, first_page_of(kSecOther) + 1);
  hide(message);
  arm_visible_reads();
  set_grid_visible(true);
  publish_status();
  (void)supported;
}

void ui_set_status(const char* text) { lv_label_set_text(status, text != nullptr ? text : ""); }

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
  if (message != nullptr && lv_obj_has_flag(message, LV_OBJ_FLAG_HIDDEN)) {
    refresh_tiles();
  }
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
  if (message != nullptr && lv_obj_has_flag(message, LV_OBJ_FLAG_HIDDEN)) {
    refresh_tiles();
  }
}
