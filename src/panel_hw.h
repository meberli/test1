#pragma once

#include <stdint.h>

// Bring up the ST7701 RGB panel, GT911 touch, and LVGL display driver.
// Does not configure the relay GPIOs.
bool panel_display_begin();
uint8_t panel_touch_begin();
bool panel_touch_ok();
bool panel_lvgl_start();
