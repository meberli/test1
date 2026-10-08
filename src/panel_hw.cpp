#include "panel_hw.h"

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <TAMC_GT911.h>
#include <Wire.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

#include "board_pins.h"

namespace {

Arduino_DataBus* bus = nullptr;
Arduino_ESP32RGBPanel* rgbpanel = nullptr;
Arduino_RGB_Display* gfx = nullptr;

// INT and RST are -1. TAMC_GT911 stores them and its reset() ignores pins the
// ESP32 rejects. That is the published ha5dzs configuration for this board.
TAMC_GT911 touch(static_cast<uint8_t>(kPinTpSda), static_cast<uint8_t>(kPinTpScl),
                 static_cast<uint8_t>(kPinTpInt), static_cast<uint8_t>(kPinTpRst), kPanelWidth, kPanelHeight);

bool touch_ok = false;

lv_disp_draw_buf_t draw_buf;
lv_disp_drv_t disp_drv;
lv_indev_drv_t indev_drv;

void panel_flush(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color) {
  int32_t w = area->x2 - area->x1 + 1;
  int32_t h = area->y2 - area->y1 + 1;
  gfx->draw16bitRGBBitmap(area->x1, area->y1, reinterpret_cast<uint16_t*>(color), static_cast<int16_t>(w),
                          static_cast<int16_t>(h));
  lv_disp_flush_ready(drv);
}

void panel_touch_read(lv_indev_drv_t*, lv_indev_data_t* data) {
  if (!touch_ok) {
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }
  touch.read();
  if (touch.isTouched && touch.touches > 0) {
    int x = touch.points[0].x;
    int y = touch.points[0].y;
    if (x < 0) {
      x = 0;
    }
    if (y < 0) {
      y = 0;
    }
    if (x >= kPanelWidth) {
      x = kPanelWidth - 1;
    }
    if (y >= kPanelHeight) {
      y = kPanelHeight - 1;
    }
    data->point.x = static_cast<lv_coord_t>(x);
    data->point.y = static_cast<lv_coord_t>(y);
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

bool probe_address(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

}  // namespace

bool panel_display_begin() {
  // Same construction as Arduino_GFX ESP32_4848S040_86BOX_GUITION, including
  // st7701_type9_init_operations and rotation 1. Timings are that device's.
  bus = new Arduino_SWSPI(GFX_NOT_DEFINED /* DC */, kPinLcdCs, kPinLcdSck, kPinLcdMosi, GFX_NOT_DEFINED /* MISO */);
  rgbpanel = new Arduino_ESP32RGBPanel(
      kPinDe, kPinVsync, kPinHsync, kPinPclk, kPinR0, kPinR1, kPinR2, kPinR3, kPinR4, kPinG0, kPinG1, kPinG2,
      kPinG3, kPinG4, kPinG5, kPinB0, kPinB1, kPinB2, kPinB3, kPinB4, 1 /* hsync_polarity */, 10 /* hsync_front_porch */,
      8 /* hsync_pulse_width */, 50 /* hsync_back_porch */, 1 /* vsync_polarity */, 10 /* vsync_front_porch */,
      8 /* vsync_pulse_width */, 20 /* vsync_back_porch */, 0 /* pclk_active_neg */, 12000000 /* prefer_speed */,
      false /* useBigEndian */, 0 /* de_idle_high */, 0 /* pclk_idle_high */, 0 /* bounce_buffer_size_px */);
  gfx = new Arduino_RGB_Display(kPanelWidth, kPanelHeight, rgbpanel, 1 /* rotation */, true /* auto_flush */, bus,
                                GFX_NOT_DEFINED /* RST */, st7701_type9_init_operations,
                                sizeof(st7701_type9_init_operations));
  if (bus == nullptr || rgbpanel == nullptr || gfx == nullptr) {
    return false;
  }

  pinMode(kPinBacklight, OUTPUT);
  digitalWrite(kPinBacklight, HIGH);
  return gfx->begin();
}

uint8_t panel_touch_begin() {
  touch_ok = false;
  Wire.begin(kPinTpSda, kPinTpScl);
  Wire.setClock(100000);

  uint8_t found = 0;
  if (probe_address(GT911_ADDR1)) {
    found = GT911_ADDR1;
  } else if (probe_address(GT911_ADDR2)) {
    found = GT911_ADDR2;
  }
  if (found == 0) {
    Serial.println("GT911 not found at 0x5D or 0x14");
    return 0;
  }

  touch.begin(found);
  // Display rotation in the Arduino_GFX device entry is 1. ha5dzs maps that
  // to TAMC rotation 2 (ROTATION_RIGHT). Not verified on this exact unit.
  touch.setRotation(ROTATION_RIGHT);
  touch.setResolution(kPanelWidth, kPanelHeight);
  touch_ok = true;
  Serial.printf("GT911 at 0x%02X\n", found);
  return found;
}

bool panel_touch_ok() { return touch_ok; }

bool panel_lvgl_start() {
  lv_init();
  constexpr uint32_t kLines = 40;
  uint32_t pixels = static_cast<uint32_t>(kPanelWidth) * kLines;
  lv_color_t* buf = static_cast<lv_color_t*>(
      heap_caps_malloc(pixels * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (buf == nullptr) {
    return false;
  }
  lv_disp_draw_buf_init(&draw_buf, buf, nullptr, pixels);
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = kPanelWidth;
  disp_drv.ver_res = kPanelHeight;
  disp_drv.flush_cb = panel_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = panel_touch_read;
  lv_indev_drv_register(&indev_drv);
  return true;
}
