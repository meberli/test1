#pragma once
// Runtime diagnostics on the serial log (all builds): touch driver counters,
// UI loop timing, task CPU load. Plus driver-level touch injection for the
// release image (console line "DIAGTAP x y" / "DIAGSWIPE x1 y1 x2 y2 ms"):
// the injected points enter gt_poll() exactly where GT911 reports land, so
// the whole path (power/wake logic, gestures, LVGL indev) is exercised.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void diag_start();                       // after the UI task runs
void diag_touch_poll(bool i2c_ok, bool fresh, bool pressed);  // gt_poll
bool diag_touch_inject(int* x, int* y, bool* pressed);        // gt_poll: true = use injected point
void diag_ui_loop(uint32_t lv_us, uint32_t poll_us, uint32_t loop_us);
void diag_ui_first_interactive();        // home screen shown and touch read once

// panel_hw.cpp: raw GT911 register read for the DIAGGT dump.
bool panel_gt911_read(uint16_t reg, uint8_t* out, size_t n);
