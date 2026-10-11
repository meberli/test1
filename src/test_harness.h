#pragma once

#include <lvgl.h>

// Serial test harness (build flag TEST_HARNESS). Line commands:
//   PING                      -> PONG
//   SHOT                      -> PNG source: base64 lines with CRC (see test_harness.cpp)
//   SHOTR off n               -> resend lines of the last SHOT
//   TAP x y                   -> press 120 ms, release
//   PRESS x y / RELEASE       -> hold / let go
//   SWIPE x1 y1 x2 y2 ms      -> drag, then release
// Each touch command answers OK or ERR <reason>. Real GT911 touch works
// whenever no simulated touch is active.
#ifdef TEST_HARNESS
void harness_poll();
bool harness_touch_override(lv_indev_data_t* data);
#else
inline void harness_poll() {}
inline bool harness_touch_override(lv_indev_data_t*) { return false; }
#endif
