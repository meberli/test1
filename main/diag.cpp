#include "diag.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <atomic>

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "sys.h"
#include "test_harness.h"

namespace {
std::atomic<uint32_t> t_reads{0}, t_err{0}, t_fresh{0}, t_pressed{0};
std::atomic<uint32_t> ui_loops{0}, ui_lv_max{0}, ui_poll_max{0}, ui_loop_max{0}, ui_slow{0};
volatile uint32_t interactive_ms = 0;

// Injection (diag task writes, UI task reads).
volatile bool inj_active = false;
volatile bool inj_raw = false;
volatile int inj_x0, inj_y0, inj_x1, inj_y1;
volatile uint32_t inj_t0, inj_dur, inj_hold;

void stats() {
#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
  static char buf[1536];
  vTaskGetRunTimeStats(buf);
  console_puts("DIAG tasks (name, abs time, % since boot):");
  console_write(buf, strlen(buf));
#endif
}

void handle(const char* line) {
  int a, b, c, d, e;
  if (sscanf(line, "DIAGRAW %d %d", &a, &b) == 2) {
    inj_x0 = a; inj_x1 = a; inj_y0 = b; inj_y1 = b; inj_dur = 120; inj_hold = 0;
    inj_raw = true;
    inj_t0 = now_ms();
    inj_active = true;
    console_puts("DIAG OK");
    return;
  }
  inj_raw = false;
  if (sscanf(line, "DIAGSWIPE %d %d %d %d %d", &a, &b, &c, &d, &e) == 5) {
    inj_x0 = a; inj_y0 = b; inj_x1 = c; inj_y1 = d; inj_dur = e < 20 ? 20 : e; inj_hold = 0;
    inj_t0 = now_ms();
    inj_active = true;
    console_puts("DIAG OK");
  } else if (sscanf(line, "DIAGTAP %d %d", &a, &b) == 2) {
    inj_x0 = a; inj_x1 = a; inj_y0 = b; inj_y1 = b; inj_dur = 120; inj_hold = 0;
    inj_t0 = now_ms();
    inj_active = true;
    console_puts("DIAG OK");
  } else if (strcmp(line, "DIAGGT") == 0) {
    uint8_t id[14] = {}, cfg[16] = {}, cmd = 0xFF, st = 0xFF;
    bool a1 = panel_gt911_read(0x8140, id, sizeof(id));
    bool a2 = panel_gt911_read(0x8047, cfg, sizeof(cfg));
    bool a3 = panel_gt911_read(0x8040, &cmd, 1);
    bool a4 = panel_gt911_read(0x814E, &st, 1);
    console_printf("DIAG GT911 ok=%d%d%d%d product=%c%c%c%c fw=0x%02X%02X xres=%u yres=%u vendor=0x%02X\n", a1, a2, a3, a4,
                   id[0], id[1], id[2], id[3], id[5], id[4], id[6] | (id[7] << 8), id[8] | (id[9] << 8), id[10]);
    console_printf("DIAG GT911 cfg_ver=0x%02X xmax=%u ymax=%u touches=%u sw1=0x%02X sw2=0x%02X shake=0x%02X filter=0x%02X "
                   "large=0x%02X noise=0x%02X cmd=0x%02X status=0x%02X\n", cfg[0], cfg[1] | (cfg[2] << 8),
                   cfg[3] | (cfg[4] << 8), cfg[5], cfg[6], cfg[7], cfg[8], cfg[9], cfg[10], cfg[11], cmd, st);
  } else if (strcmp(line, "DIAGSTATS") == 0) {
    stats();
  }
}

void report(uint32_t t) {
  console_printf("DIAG t=%lus touch reads=%lu i2c_err=%lu reports=%lu pressed=%lu | ui loops=%lu lv_max=%lums "
                 "poll_max=%lums loop_max=%lums slow(>100ms)=%lu | int=%uKB psram=%uKB | interactive=%lums\n",
                 (unsigned long)t, (unsigned long)t_reads.load(), (unsigned long)t_err.load(), (unsigned long)t_fresh.load(),
                 (unsigned long)t_pressed.load(), (unsigned long)ui_loops.load(), (unsigned long)(ui_lv_max.load() / 1000),
                 (unsigned long)(ui_poll_max.load() / 1000), (unsigned long)(ui_loop_max.load() / 1000), (unsigned long)ui_slow.load(),
                 (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                 (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024), (unsigned long)interactive_ms);
  ui_lv_max = ui_poll_max = ui_loop_max = 0;
}

void diag_task(void*) {
  char line[64];
  size_t len = 0;
  uint32_t start = now_ms(), last = start;
  bool stats30 = false, stats90 = false;
  for (;;) {
#ifndef CONFIG_PANEL_TEST_HARNESS
    // The release image has no harness: read the console here.
    while (console_available() > 0) {
      int ch = console_read();
      if (ch == '\r') continue;
      if (ch == '\n') {
        line[len] = '\0';
        handle(line);
        len = 0;
      } else if (len < sizeof(line) - 1) {
        line[len++] = static_cast<char>(ch);
      }
    }
#endif
    uint32_t now = now_ms();
    uint32_t up = (now - start) / 1000;
    uint32_t every = up < 120 ? 5000 : 60000;
    if (now - last >= every) {
      last = now;
      report(up);
    }
    if (!stats30 && up >= 30) { stats30 = true; stats(); }
    if (!stats90 && up >= 90) { stats90 = true; stats(); }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
}  // namespace

void diag_start() { xTaskCreatePinnedToCore(diag_task, "diag", 4096, nullptr, 2, nullptr, 0); }

void diag_touch_poll(bool i2c_ok, bool fresh, bool pressed) {
  ++t_reads;
  if (!i2c_ok) ++t_err;
  if (fresh) ++t_fresh;
  if (pressed) ++t_pressed;
}

bool diag_touch_inject(int* x, int* y, bool* pressed, bool* raw) {
  if (!inj_active) return false;
  *raw = inj_raw;
  uint32_t el = now_ms() - inj_t0;
  if (el <= inj_dur) {
    *x = inj_x0 + (inj_x1 - inj_x0) * (int)el / (int)inj_dur;
    *y = inj_y0 + (inj_y1 - inj_y0) * (int)el / (int)inj_dur;
    *pressed = true;
  } else {
    *x = inj_x1;
    *y = inj_y1;
    *pressed = false;
    if (el > inj_dur + 60) inj_active = false;
  }
  return true;
}

void diag_ui_loop(uint32_t lv_us, uint32_t poll_us, uint32_t loop_us) {
  ++ui_loops;
  if (lv_us > ui_lv_max) ui_lv_max = lv_us;
  if (poll_us > ui_poll_max) ui_poll_max = poll_us;
  if (loop_us > ui_loop_max) ui_loop_max = loop_us;
  if (loop_us > 100000) ++ui_slow;
}

void diag_ui_first_interactive() {
  if (interactive_ms == 0) interactive_ms = now_ms();
}
