// ESP32-C3 Round RuuviTag Display Hub (GC9A01 240x240 + CST816S touch)
// Requires LVGL 8.3.x, TFT_eSPI, NimBLE-Arduino 1.4.x. See README.md.

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <lvgl.h>
#include "config.h"
#include "settings.h"
#include "backlight.h"
#include "touch_cst816s.h"
#include "wifi_manager.h"
#include "ruuvi_ble.h"
#include "gui_ruuvi.h"

TFT_eSPI tft = TFT_eSPI();

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf1[240 * 20];
static lv_color_t buf2[240 * 20];      // double buffering

static void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors((uint16_t *)&color_p->full, w * h, true);
  tft.endWrite();
  lv_disp_flush_ready(disp);
}

static void my_touch_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
  static uint16_t lx = 0, ly = 0;
  static bool was_down = false;
  uint16_t x, y;
  if (touch_read(&x, &y)) {
    lx = x;
    ly = y;
    data->state = LV_INDEV_STATE_PR;
    if (!was_down) gui_notify_touch();
    was_down = true;
  } else {
    data->state = LV_INDEV_STATE_REL;
    was_down = false;
  }
  data->point.x = lx;
  data->point.y = ly;
}

void setup() {
  Serial.begin(115200);

  settings_load();

  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  backlight_init(0);                    // keep dark until the first frame is drawn

  lv_init();
  lv_disp_draw_buf_init(&draw_buf, buf1, buf2, 240 * 20);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = 240;
  disp_drv.ver_res = 240;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  if (!touch_init()) Serial.println("WARNING: CST816S touch controller not found (check pins in config.h)");
  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touch_read;
  lv_indev_drv_register(&indev_drv);

  gui_init_ruuvi_hub();
  lv_timer_handler();                   // render first frame
  backlight_set(g_settings.brightness);

  init_wifi_manager();
  init_ruuvi_ble();
}

void loop() {
  static uint32_t last = millis();
  uint32_t now = millis();
#if !(defined(LV_TICK_CUSTOM) && LV_TICK_CUSTOM)
  lv_tick_inc(now - last);              // LVGL needs a time base
#endif
  last = now;

  uint32_t wait = lv_timer_handler();
  tick_wifi_manager();
  tick_ruuvi_ble();
  settings_tick();

  if (wait < 2) wait = 2;
  if (wait > 15) wait = 15;
  delay(wait);
}
