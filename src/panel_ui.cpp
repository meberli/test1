#include "panel_ui.h"

#include <Arduino.h>
#include <lvgl.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "font_dejavu.h"

namespace {

constexpr int kScreen = 480;
constexpr int kHeaderH = 80;
constexpr int kFooterY = 392;
constexpr int kMargin = 12;
constexpr int kTileW = 222;
constexpr int kTileH = 140;
constexpr int kRowsPerPage = 2;
constexpr int kPerPage = 4;
constexpr uint32_t kShutterRereadMs = 3000;

const char* kShutterLabel[3] = {"Up", "Stop", "Down"};
const char* kShutterCommand[3] = {"FullUp", "Stop", "FullDown"};
const uint32_t kShutterColor[3] = {0x245A8D, 0x8A6230, 0x1F6B45};

lv_obj_t* heading = nullptr;
lv_obj_t* status = nullptr;
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

LoxoneControl controls[kLoxoneControlCap];
size_t control_count = 0;
size_t supported_count = 0;
RowPlan rows[kLoxoneControlCap];
int row_count = 0;
int page = 0;
bool known_on[kLoxoneControlCap] = {};
bool known[kLoxoneControlCap] = {};
bool state_queried[kLoxoneControlCap] = {};
bool moving[kLoxoneControlCap] = {};
int position_pct[kLoxoneControlCap] = {};
uint32_t reread_after[kLoxoneControlCap] = {};
bool settling = false;
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

int page_count() {
  if (row_count <= 0) {
    return 1;
  }
  return (row_count + kRowsPerPage - 1) / kRowsPerPage;
}

void rebuild_rows() {
  row_count = 0;
  size_t i = 0;
  while (i < control_count && row_count < static_cast<int>(kLoxoneControlCap)) {
    if (controls[i].kind == kLoxoneShutter) {
      rows[row_count].shutter = true;
      rows[row_count].a = static_cast<int>(i);
      rows[row_count].b = -1;
      row_count++;
      i++;
      continue;
    }
    int next = -1;
    if (i + 1 < control_count && controls[i + 1].kind != kLoxoneShutter) {
      next = static_cast<int>(i + 1);
    }
    rows[row_count].shutter = false;
    rows[row_count].a = static_cast<int>(i);
    rows[row_count].b = next;
    row_count++;
    i += next >= 0 ? 2 : 1;
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
  const char* state_text = "...";
  if (known[index]) {
    state_text = known_on[index] ? "On" : "Off";
  } else if (state_queried[index]) {
    state_text = "n/a";
  }
  if (ctrl.room[0] != '\0') {
    snprintf(out, n, "%s - %s", ctrl.room, state_text);
  } else {
    snprintf(out, n, "%s", state_text);
  }
}

void format_shutter_pos(char* out, size_t n, int index) {
  if (moving[index]) {
    snprintf(out, n, "Moving");
    return;
  }
  if (!known[index]) {
    snprintf(out, n, "%s", state_queried[index] ? "n/a" : "...");
    return;
  }
  int pct = position_pct[index];
  if (pct <= 2) {
    snprintf(out, n, "Open");
  } else if (pct >= 98) {
    snprintf(out, n, "Closed");
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

void show_switch_tile(int slot, int index) {
  show(tiles[slot]);
  const LoxoneControl& ctrl = controls[index];
  lv_label_set_text(tile_name[slot], ctrl.name);
  char sub[96];
  if (ctrl.kind == kLoxoneSwitch) {
    format_switch_sub(sub, sizeof(sub), index);
  } else if (ctrl.room[0] != '\0') {
    snprintf(sub, sizeof(sub), "%s", ctrl.room);
  } else {
    snprintf(sub, sizeof(sub), "Button");
  }
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

void publish_load_status() {
  if (!settling) {
    return;
  }
  size_t pending = 0;
  size_t readable = 0;
  size_t unknown_switches = 0;
  for (size_t i = 0; i < control_count; ++i) {
    bool has_state = controls[i].state[0] != '\0' && controls[i].state_kind != kLoxoneStateNone;
    if (has_state) {
      readable++;
    }
    if (has_state && !state_queried[i] && reread_after[i] == 0) {
      pending++;
    }
    if (controls[i].kind == kLoxoneSwitch && !known[i]) {
      unknown_switches++;
    }
  }
  if (pending > 0) {
    ui_set_status("Reading status");
    return;
  }
  settling = false;
  if (supported_count > control_count) {
    char line[64];
    snprintf(line, sizeof(line), "Showing %u of %u", static_cast<unsigned>(control_count),
             static_cast<unsigned>(supported_count));
    ui_set_status(line);
  } else if (readable > 0 && unknown_switches > 0) {
    ui_set_status("Some lights did not report status");
  } else {
    ui_set_status("");
  }
}

void refresh_tiles() {
  int pages = page_count();
  if (page >= pages) {
    page = pages - 1;
  }
  if (page < 0) {
    page = 0;
  }
  for (int slot = 0; slot < kPerPage; ++slot) {
    hide(tiles[slot]);
  }
  for (int row_slot = 0; row_slot < kRowsPerPage; ++row_slot) {
    hide(shutter_box[row_slot]);
    int row_index = page * kRowsPerPage + row_slot;
    if (row_index < 0 || row_index >= row_count) {
      continue;
    }
    if (rows[row_index].shutter) {
      show_shutter_row(row_slot, rows[row_index].a);
      continue;
    }
    if (rows[row_index].a >= 0) {
      show_switch_tile(row_slot * 2, rows[row_index].a);
    }
    if (rows[row_index].b >= 0) {
      show_switch_tile(row_slot * 2 + 1, rows[row_index].b);
    }
  }

  if (control_count == 0 || pages <= 1) {
    hide(prev_btn);
    hide(next_btn);
    hide(page_label);
  } else {
    show(prev_btn);
    show(next_btn);
    show(page_label);
    char text[16];
    snprintf(text, sizeof(text), "%d / %d", page + 1, pages);
    lv_label_set_text(page_label, text);
    if (page == 0) {
      lv_obj_add_state(prev_btn, LV_STATE_DISABLED);
    } else {
      lv_obj_clear_state(prev_btn, LV_STATE_DISABLED);
    }
    if (page + 1 >= pages) {
      lv_obj_add_state(next_btn, LV_STATE_DISABLED);
    } else {
      lv_obj_clear_state(next_btn, LV_STATE_DISABLED);
    }
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
    return;
  }
  refresh_tiles();
}

int control_for_slot(int slot) {
  int row_slot = slot / 2;
  int col = slot % 2;
  int row_index = page * kRowsPerPage + row_slot;
  if (row_index < 0 || row_index >= row_count || rows[row_index].shutter) {
    return -1;
  }
  return col == 0 ? rows[row_index].a : rows[row_index].b;
}

void queue_command(int index, const char* command) {
  snprintf(pending.action, sizeof(pending.action), "%s", controls[index].action);
  snprintf(pending.command, sizeof(pending.command), "%s", command);
  pending.index = index;
  command_pending = true;
  if (!settling) {
    ui_set_status("Sending");
  }
}

void on_tile(lv_event_t* event) {
  if (command_pending) {
    return;
  }
  int slot = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
  int index = control_for_slot(slot);
  if (index < 0 || static_cast<size_t>(index) >= control_count) {
    return;
  }
  const LoxoneControl& ctrl = controls[index];
  const char* command = "Pulse";
  if (ctrl.kind == kLoxoneSwitch) {
    command = (known[index] && known_on[index]) ? "Off" : "On";
  }
  queue_command(index, command);
  lv_label_set_text(tile_sub[slot], "Sending");
}

void on_shutter(lv_event_t* event) {
  if (command_pending) {
    return;
  }
  intptr_t tag = reinterpret_cast<intptr_t>(lv_event_get_user_data(event));
  int row_slot = static_cast<int>(tag / 4);
  int which = static_cast<int>(tag % 4);
  if (row_slot < 0 || row_slot >= kRowsPerPage || which < 0 || which > 2) {
    return;
  }
  int row_index = page * kRowsPerPage + row_slot;
  if (row_index < 0 || row_index >= row_count || !rows[row_index].shutter) {
    return;
  }
  int index = rows[row_index].a;
  queue_command(index, kShutterCommand[which]);
  lv_label_set_text(shutter_pos[row_slot], "Sending");
}

void on_prev(lv_event_t*) {
  if (page > 0) {
    page--;
    refresh_tiles();
  }
}

void on_next(lv_event_t*) {
  if (page + 1 < page_count()) {
    page++;
    refresh_tiles();
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
  if (ctrl.state_kind == kLoxoneStateScene) {
    known_on[index] = parsed != 0.f;
  } else {
    known_on[index] = parsed > 0.f;
  }
  known[index] = true;
  return true;
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

    for (int which = 0; which < 3; ++which) {
      lv_obj_t* btn = lv_btn_create(box);
      lv_obj_set_pos(btn, 8 + which * 148, 48);
      lv_obj_set_size(btn, 140, 80);
      style_shutter_button(btn, kShutterColor[which]);
      intptr_t tag = static_cast<intptr_t>(row_slot * 4 + which);
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
  lv_label_set_text(prev_label, "Prev");
  lv_obj_center(prev_label);

  next_btn = lv_btn_create(screen);
  lv_obj_set_pos(next_btn, 328, kFooterY);
  lv_obj_set_size(next_btn, 140, 72);
  style_chrome_button(next_btn);
  lv_obj_add_event_cb(next_btn, on_next, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* next_label = make_label(next_btn, &font_dejavu_20, lv_color_hex(0xFFFFFF));
  lv_label_set_text(next_label, "Next");
  lv_obj_center(next_label);

  page_label = make_label(screen, &font_dejavu_14, lv_color_hex(0xC5CED6));
  lv_obj_align(page_label, LV_ALIGN_BOTTOM_MID, 0, -28);
  hide(prev_btn);
  hide(next_btn);
  hide(page_label);

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

void ui_show_controls(const LoxoneControl* items, size_t count, size_t supported, const char* server_name) {
  if (count > kLoxoneControlCap) {
    count = kLoxoneControlCap;
  }
  control_count = count;
  supported_count = supported;
  page = 0;
  command_pending = false;
  settling = true;
  memset(known, 0, sizeof(known));
  memset(known_on, 0, sizeof(known_on));
  memset(state_queried, 0, sizeof(state_queried));
  memset(moving, 0, sizeof(moving));
  memset(position_pct, 0, sizeof(position_pct));
  memset(reread_after, 0, sizeof(reread_after));
  for (size_t i = 0; i < count; ++i) {
    controls[i] = items[i];
    if (controls[i].state[0] == '\0' || controls[i].state_kind == kLoxoneStateNone) {
      state_queried[i] = true;
    }
  }
  rebuild_rows();
  lv_label_set_text(heading, (server_name != nullptr && server_name[0] != '\0') ? server_name : "Loxone");
  if (count == 0) {
    settling = false;
    ui_set_status("No controls to show");
    ui_show_message("Connected",
                    "The Miniserver answered, but LoxAPP3.json has no switch, light, button, or shutter controls.",
                    false);
    return;
  }
  hide(message);
  set_grid_visible(true);
  publish_load_status();
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
    } else if (strcmp(command, "Off") == 0) {
      known[index] = true;
      known_on[index] = false;
    } else if (controls[index].kind == kLoxoneShutter) {
      moving[index] = true;
      reread_after[index] = millis() + kShutterRereadMs;
    }
  }
  if (!settling) {
    if (ok) {
      if (command != nullptr && strcmp(command, "Pulse") == 0) {
        ui_set_status("Pulse sent");
      } else if (command != nullptr && strcmp(command, "FullUp") == 0) {
        ui_set_status("Shutter up");
      } else if (command != nullptr && strcmp(command, "FullDown") == 0) {
        ui_set_status("Shutter down");
      } else if (command != nullptr && strcmp(command, "Stop") == 0) {
        ui_set_status("Shutter stopped");
      } else {
        ui_set_status("Command sent");
      }
    } else {
      ui_set_status(detail != nullptr && detail[0] != '\0' ? detail : "Command failed");
    }
  }
  if (message != nullptr && lv_obj_has_flag(message, LV_OBJ_FLAG_HIDDEN)) {
    refresh_tiles();
  }
}

bool ui_take_state(int* index, char* state_id, size_t state_n) {
  if (index == nullptr || state_id == nullptr || state_n == 0 || command_pending) {
    return false;
  }
  uint32_t now = millis();
  for (size_t i = 0; i < control_count; ++i) {
    if (controls[i].state[0] == '\0' || controls[i].state_kind == kLoxoneStateNone) {
      continue;
    }
    if (reread_after[i] != 0 && static_cast<int32_t>(now - reread_after[i]) < 0) {
      continue;
    }
    bool due = reread_after[i] != 0;
    if (state_queried[i] && !due) {
      continue;
    }
    *index = static_cast<int>(i);
    snprintf(state_id, state_n, "%s", controls[i].state);
    return true;
  }
  return false;
}

void ui_state_finished(int index, bool ok, const char* value) {
  if (index < 0 || static_cast<size_t>(index) >= control_count) {
    return;
  }
  state_queried[index] = true;
  reread_after[index] = 0;
  moving[index] = false;
  if (ok) {
    apply_state_value(index, value);
  }
  publish_load_status();
  if (message != nullptr && lv_obj_has_flag(message, LV_OBJ_FLAG_HIDDEN)) {
    refresh_tiles();
  }
}
