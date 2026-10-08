#include "panel_ui.h"

#include <lvgl.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace {

constexpr int kScreen = 480;
constexpr int kHeaderH = 80;
constexpr int kFooterY = 392;
constexpr int kMargin = 12;
constexpr int kTileW = 222;
constexpr int kTileH = 140;
constexpr int kPerPage = 4;

lv_obj_t* heading = nullptr;
lv_obj_t* status = nullptr;
lv_obj_t* tiles[kPerPage] = {};
lv_obj_t* tile_name[kPerPage] = {};
lv_obj_t* tile_sub[kPerPage] = {};
lv_obj_t* prev_btn = nullptr;
lv_obj_t* next_btn = nullptr;
lv_obj_t* page_label = nullptr;
lv_obj_t* message = nullptr;
lv_obj_t* message_title = nullptr;
lv_obj_t* message_body = nullptr;
lv_obj_t* retry_btn = nullptr;

LoxoneControl controls[kLoxoneControlCap];
size_t control_count = 0;
size_t supported_count = 0;
int page = 0;
bool known_on[kLoxoneControlCap] = {};
bool known[kLoxoneControlCap] = {};
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

int page_count() {
  if (control_count == 0) {
    return 1;
  }
  return static_cast<int>((control_count + kPerPage - 1) / kPerPage);
}

void style_tile(lv_obj_t* btn, LoxoneKind kind, bool is_known, bool on) {
  lv_color_t bg = lv_color_hex(0x243044);
  if (kind == kLoxonePulse) {
    bg = lv_color_hex(0x1B3A4B);
  } else if (is_known && on) {
    bg = lv_color_hex(0x2F7D32);
  } else if (is_known && !on) {
    bg = lv_color_hex(0x1A2330);
  }
  lv_obj_set_style_bg_color(btn, bg, 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x3E6B45), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn, 18, 0);
  lv_obj_set_style_border_width(btn, 0, 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  lv_obj_set_style_pad_all(btn, 8, 0);
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
    size_t index = static_cast<size_t>(page * kPerPage + slot);
    if (index >= control_count) {
      hide(tiles[slot]);
      continue;
    }
    show(tiles[slot]);
    const LoxoneControl& ctrl = controls[index];
    lv_label_set_text(tile_name[slot], ctrl.name);
    if (ctrl.kind == kLoxoneSwitch && known[index]) {
      lv_label_set_text(tile_sub[slot], known_on[index] ? "On" : "Off");
    } else if (ctrl.room[0] != '\0') {
      lv_label_set_text(tile_sub[slot], ctrl.room);
    } else if (ctrl.kind == kLoxonePulse) {
      lv_label_set_text(tile_sub[slot], "Button");
    } else {
      lv_label_set_text(tile_sub[slot], "Switch");
    }
    style_tile(tiles[slot], ctrl.kind, known[index], known_on[index]);
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
    hide(prev_btn);
    hide(next_btn);
    hide(page_label);
    return;
  }
  refresh_tiles();
}

void on_tile(lv_event_t* event) {
  if (command_pending) {
    return;
  }
  int slot = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
  int index = page * kPerPage + slot;
  if (index < 0 || static_cast<size_t>(index) >= control_count) {
    return;
  }
  const LoxoneControl& ctrl = controls[index];
  const char* command = "Pulse";
  if (ctrl.kind == kLoxoneSwitch) {
    command = (known[index] && known_on[index]) ? "Off" : "On";
  }
  snprintf(pending.action, sizeof(pending.action), "%s", ctrl.action);
  snprintf(pending.command, sizeof(pending.command), "%s", command);
  pending.index = index;
  command_pending = true;
  lv_label_set_text(tile_sub[slot], "Sending");
  ui_set_status("Sending");
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

}  // namespace

void ui_init() {
  lv_obj_t* screen = lv_scr_act();
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x101418), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

  heading = make_label(screen, &lv_font_montserrat_20, lv_color_hex(0xF2F5F7));
  lv_obj_set_pos(heading, 16, 12);
  lv_label_set_text(heading, "Loxone");

  status = make_label(screen, &lv_font_montserrat_14, lv_color_hex(0xA8B3BD));
  lv_obj_set_pos(status, 16, 44);
  lv_obj_set_width(status, 448);
  lv_label_set_long_mode(status, LV_LABEL_LONG_DOT);
  lv_label_set_text(status, "");

  for (int slot = 0; slot < kPerPage; ++slot) {
    int col = slot % 2;
    int row = slot / 2;
    int x = kMargin + col * (kTileW + kMargin);
    int y = kHeaderH + kMargin + row * (kTileH + kMargin);
    lv_obj_t* btn = lv_btn_create(screen);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, kTileW, kTileH);
    lv_obj_add_event_cb(btn, on_tile, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<intptr_t>(slot)));
    tiles[slot] = btn;

    tile_name[slot] = make_label(btn, &lv_font_montserrat_20, lv_color_hex(0xFFFFFF));
    lv_obj_set_width(tile_name[slot], kTileW - 24);
    lv_label_set_long_mode(tile_name[slot], LV_LABEL_LONG_WRAP);
    lv_obj_align(tile_name[slot], LV_ALIGN_CENTER, 0, -16);

    tile_sub[slot] = make_label(btn, &lv_font_montserrat_14, lv_color_hex(0xD5DDE4));
    lv_obj_set_width(tile_sub[slot], kTileW - 24);
    lv_label_set_long_mode(tile_sub[slot], LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(tile_sub[slot], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(tile_sub[slot], LV_ALIGN_CENTER, 0, 28);
    hide(btn);
  }

  prev_btn = lv_btn_create(screen);
  lv_obj_set_pos(prev_btn, 12, kFooterY);
  lv_obj_set_size(prev_btn, 140, 72);
  style_chrome_button(prev_btn);
  lv_obj_add_event_cb(prev_btn, on_prev, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* prev_label = make_label(prev_btn, &lv_font_montserrat_20, lv_color_hex(0xFFFFFF));
  lv_label_set_text(prev_label, "Prev");
  lv_obj_center(prev_label);

  next_btn = lv_btn_create(screen);
  lv_obj_set_pos(next_btn, 328, kFooterY);
  lv_obj_set_size(next_btn, 140, 72);
  style_chrome_button(next_btn);
  lv_obj_add_event_cb(next_btn, on_next, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* next_label = make_label(next_btn, &lv_font_montserrat_20, lv_color_hex(0xFFFFFF));
  lv_label_set_text(next_label, "Next");
  lv_obj_center(next_label);

  page_label = make_label(screen, &lv_font_montserrat_14, lv_color_hex(0xC5CED6));
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

  message_body = make_label(message, &lv_font_montserrat_20, lv_color_hex(0xE6EDF2));
  lv_obj_set_width(message_body, 400);
  lv_label_set_long_mode(message_body, LV_LABEL_LONG_WRAP);
  lv_obj_align(message_body, LV_ALIGN_TOP_LEFT, 8, 72);

  retry_btn = lv_btn_create(message);
  lv_obj_set_size(retry_btn, 400, 88);
  lv_obj_align(retry_btn, LV_ALIGN_BOTTOM_MID, 0, -8);
  style_chrome_button(retry_btn);
  lv_obj_add_event_cb(retry_btn, on_retry, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* retry_label = make_label(retry_btn, &lv_font_montserrat_20, lv_color_hex(0xFFFFFF));
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
  memset(known, 0, sizeof(known));
  memset(known_on, 0, sizeof(known_on));
  for (size_t i = 0; i < count; ++i) {
    controls[i] = items[i];
  }
  lv_label_set_text(heading, (server_name != nullptr && server_name[0] != '\0') ? server_name : "Loxone");
  if (count == 0) {
    ui_set_status("No switch or button controls");
    ui_show_message("Connected",
                    "The Miniserver answered, but LoxAPP3.json has no switch, light, or button controls.", false);
    return;
  }
  char line[96];
  if (supported > count) {
    snprintf(line, sizeof(line), "Showing %u of %u. First tap sends On.", static_cast<unsigned>(count),
             static_cast<unsigned>(supported));
  } else {
    snprintf(line, sizeof(line), "First tap sends On.");
  }
  ui_set_status(line);
  hide(message);
  set_grid_visible(true);
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
    }
  }
  if (ok) {
    ui_set_status(command != nullptr && strcmp(command, "Pulse") == 0 ? "Pulse sent" : "Command sent");
  } else {
    ui_set_status(detail != nullptr && detail[0] != '\0' ? detail : "Command failed");
  }
  if (message != nullptr && lv_obj_has_flag(message, LV_OBJ_FLAG_HIDDEN)) {
    refresh_tiles();
  }
}
