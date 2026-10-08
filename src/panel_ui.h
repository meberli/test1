#pragma once

#include <stddef.h>

#include "loxone_types.h"

struct LoxoneCommandRequest {
  char action[40];
  char command[16];
  int index;
};

void ui_init();
void ui_show_message(const char* title, const char* body, bool retry);
void ui_show_controls(const LoxoneControl* items, size_t count, size_t supported, const char* server_name);
void ui_set_status(const char* text);
bool ui_take_command(LoxoneCommandRequest* out);
bool ui_take_retry();
void ui_command_finished(int index, bool ok, const char* command, const char* detail);

// One unread state per call. state_id is the uuid from LoxoneControl.state.
// A shutter command schedules a later read; that also comes back through here.
bool ui_take_state(int* index, char* state_id, size_t state_n);
void ui_state_finished(int index, bool ok, const char* value);
