// LVGL 8.3.x UI for a 240x240 round display.
// Tiles (swipe left/right):  Main | Settings | Wi-Fi | Sensor
// All widgets are kept inside the visible circle (radius 120 around 120,120).

#include <Arduino.h>
#include <lvgl.h>
#include <string.h>
#include <time.h>
#include "config.h"
#include "settings.h"
#include "backlight.h"
#include "wifi_manager.h"
#include "ruuvi_ble.h"
#include "gui_ruuvi.h"

#define COL_BG_MAIN   lv_color_make(5, 10, 25)
#define COL_BG_PAGE   lv_color_make(10, 15, 35)
#define COL_PANEL     lv_color_make(15, 35, 75)
#define COL_ACCENT    lv_color_make(0, 190, 255)
#define COL_TITLE     lv_color_make(0, 150, 255)
#define COL_GREY      lv_color_make(90, 100, 120)

// ---- widgets -------------------------------------------------------------
static lv_obj_t *tv, *page_main, *page_settings, *page_wifi, *page_sensor;

static lv_obj_t *arc_hum, *lbl_temp, *lbl_hum_text, *lbl_pres, *lbl_clock;

static lv_obj_t *dd_interval, *roller_start, *roller_end, *sw_wifi_sleep;

static lv_obj_t *wifi_status_lbl, *wifi_list, *btn_scan_lbl;
static lv_obj_t *wifi_modal, *wifi_ta, *wifi_kb;

static lv_obj_t *tag_cur_lbl, *tag_list;

static lv_style_t style_roller;

// ---- state ---------------------------------------------------------------
static uint32_t night_override_until = 0;
static bool     night_now = false;

#define MAX_NETS 12
static char ssid_cache[MAX_NETS][33];
static int  ssid_count = 0;
static bool wifi_scanning = false;
static char sel_ssid[33];

static RuuviSeen seen_cache[RUUVI_MAX_SEEN];
static int       seen_n = 0;

static const int kIntervals[] = {5, 30, 60, 0};

// ---- helpers -------------------------------------------------------------
static void set_text_if_changed(lv_obj_t *l, const char *t) {
  if (strcmp(lv_label_get_text(l), t) != 0) lv_label_set_text(l, t);
}

static void set_color_if_changed(lv_obj_t *l, lv_color_t c) {
  lv_color_t cur = lv_obj_get_style_text_color(l, LV_PART_MAIN);
  if (cur.full != c.full) lv_obj_set_style_text_color(l, c, 0);
}

static lv_color_t temp_color(float t) {
  if (t < 0)   return lv_color_make(130, 200, 255);
  if (t < 20)  return lv_color_white();
  if (t < 26)  return lv_color_make(255, 220, 130);
  return lv_color_make(255, 130, 90);
}

static const char *battery_symbol(uint16_t mv) {
  if (mv >= 2900) return LV_SYMBOL_BATTERY_FULL;
  if (mv >= 2800) return LV_SYMBOL_BATTERY_3;
  if (mv >= 2700) return LV_SYMBOL_BATTERY_2;
  if (mv >= 2500) return LV_SYMBOL_BATTERY_1;
  return LV_SYMBOL_BATTERY_EMPTY;
}

static uint32_t stale_limit_ms() {
  uint32_t lim = (uint32_t)g_settings.scan_interval_s * 3000UL + 15000UL;
  return lim < 120000UL ? 120000UL : lim;
}

// ---- brightness / night mode --------------------------------------------
static bool is_night(int hr) {
  int s = g_settings.night_start, e = g_settings.night_end;
  if (s == e) return false;
  return (s > e) ? (hr >= s || hr < e) : (hr >= s && hr < e);
}

static void apply_brightness() {
  bool overriding = (int32_t)(night_override_until - millis()) > 0;
  uint8_t target = (night_now && !overriding) ? NIGHT_BRIGHTNESS : g_settings.brightness;
  backlight_set(target);
}

void gui_notify_touch(void) {
  if (night_now) {
    night_override_until = millis() + NIGHT_WAKE_MS;
    apply_brightness();
  }
}

// ---- main page events ----------------------------------------------------
static void gesture_event_cb(lv_event_t *e) {
  lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
  int b = g_settings.brightness;
  if (dir == LV_DIR_TOP)         b += BRIGHTNESS_STEP;
  else if (dir == LV_DIR_BOTTOM) b -= BRIGHTNESS_STEP;
  else return;
  if (b > 255) b = 255;
  if (b < MIN_BRIGHTNESS) b = MIN_BRIGHTNESS;
  g_settings.brightness = (uint8_t)b;
  settings_request_save();
  night_override_until = millis() + NIGHT_WAKE_MS;
  apply_brightness();
}

// ---- settings page events ------------------------------------------------
static void dd_interval_cb(lv_event_t *e) {
  uint16_t sel = lv_dropdown_get_selected(lv_event_get_target(e));
  if (sel < 4) {
    g_settings.scan_interval_s = kIntervals[sel];
    settings_request_save();
  }
}

static void roller_cb(lv_event_t *e) {
  lv_obj_t *r = lv_event_get_target(e);
  uint16_t sel = lv_roller_get_selected(r);
  if (r == roller_start) g_settings.night_start = (uint8_t)sel;
  else                   g_settings.night_end   = (uint8_t)sel;
  settings_request_save();
}

static void sw_sleep_cb(lv_event_t *e) {
  g_settings.wifi_auto_off = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
  settings_request_save();
}

// ---- wifi page -----------------------------------------------------------
static void close_wifi_modal() {
  if (wifi_modal) lv_obj_del_async(wifi_modal);
  wifi_modal = wifi_ta = wifi_kb = NULL;
}

static void wifi_kb_event_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_READY) {
    wifi_connect(sel_ssid, lv_textarea_get_text(wifi_ta));
    close_wifi_modal();
  } else if (code == LV_EVENT_CANCEL) {
    close_wifi_modal();
  }
}

static void open_password_modal() {
  wifi_modal = lv_obj_create(lv_layer_top());
  lv_obj_set_size(wifi_modal, 240, 240);
  lv_obj_center(wifi_modal);
  lv_obj_set_style_radius(wifi_modal, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(wifi_modal, COL_BG_PAGE, 0);
  lv_obj_set_style_bg_opa(wifi_modal, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(wifi_modal, 0, 0);
  lv_obj_set_style_pad_all(wifi_modal, 0, 0);
  lv_obj_clear_flag(wifi_modal, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = lv_label_create(wifi_modal);
  lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
  lv_obj_set_width(title, 110);
  lv_label_set_text(title, sel_ssid);
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(title, COL_ACCENT, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

  wifi_ta = lv_textarea_create(wifi_modal);
  lv_textarea_set_one_line(wifi_ta, true);
  lv_textarea_set_password_mode(wifi_ta, true);
  lv_textarea_set_placeholder_text(wifi_ta, "Password");
  lv_obj_set_size(wifi_ta, 160, 36);
  lv_obj_align(wifi_ta, LV_ALIGN_TOP_MID, 0, 42);

  wifi_kb = lv_keyboard_create(wifi_modal);
  lv_obj_set_size(wifi_kb, 170, 100);
  lv_obj_align(wifi_kb, LV_ALIGN_BOTTOM_MID, 0, -40);
  lv_keyboard_set_textarea(wifi_kb, wifi_ta);
  lv_obj_add_event_cb(wifi_kb, wifi_kb_event_cb, LV_EVENT_ALL, NULL);
}

static void wifi_network_select_cb(lv_event_t *e) {
  if (wifi_modal) return;
  intptr_t idx = (intptr_t)lv_event_get_user_data(e);
  if (idx < 0 || idx >= ssid_count) return;
  strlcpy(sel_ssid, ssid_cache[idx], sizeof(sel_ssid));
  open_password_modal();
}

static void scan_wifi_event_cb(lv_event_t *e) {
  if (wifi_scanning) return;
  if (wifi_scan_start()) {
    wifi_scanning = true;
    lv_list_clean(wifi_list);
    lv_label_set_text(btn_scan_lbl, "Scanning...");
  } else {
    lv_label_set_text(btn_scan_lbl, "Scan failed");
  }
}

static void populate_wifi_list(int n) {
  lv_list_clean(wifi_list);
  ssid_count = 0;
  for (int i = 0; i < n && ssid_count < MAX_NETS; i++) {
    String s = wifi_scan_ssid(i);
    if (s.length() == 0) continue;
    bool dup = false;
    for (int k = 0; k < ssid_count; k++) {
      if (strcmp(ssid_cache[k], s.c_str()) == 0) { dup = true; break; }
    }
    if (dup) continue;
    strlcpy(ssid_cache[ssid_count], s.c_str(), sizeof(ssid_cache[0]));
    lv_obj_t *b = lv_list_add_btn(wifi_list, LV_SYMBOL_WIFI, ssid_cache[ssid_count]);
    lv_obj_add_event_cb(b, wifi_network_select_cb, LV_EVENT_CLICKED, (void *)(intptr_t)ssid_count);
    ssid_count++;
  }
}

static void wifi_poll_cb(lv_timer_t *t) {
  if (!wifi_scanning) return;
  int n = wifi_scan_poll();
  if (n == -1) return;                       // still running
  wifi_scanning = false;
  if (n >= 0) {
    populate_wifi_list(n);
    lv_label_set_text(btn_scan_lbl, "Scan Wi-Fi");
  } else {
    lv_label_set_text(btn_scan_lbl, "Scan failed");
  }
  wifi_scan_finish();
}

// ---- sensor page ---------------------------------------------------------
static void fmt_mac(char *out, size_t n, const uint8_t *m) {
  snprintf(out, n, "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
}

static void tag_select_cb(lv_event_t *e);

static void refresh_tag_list(void *unused) {
  char txt[48], mac[20];
  lv_list_clean(tag_list);

  lv_obj_t *b = lv_list_add_btn(tag_list, g_settings.has_mac ? NULL : LV_SYMBOL_OK, "Auto (strongest)");
  lv_obj_add_event_cb(b, tag_select_cb, LV_EVENT_CLICKED, (void *)(intptr_t)-1);

  seen_n = ruuvi_get_seen(seen_cache, RUUVI_MAX_SEEN);
  for (int i = 0; i < seen_n; i++) {
    fmt_mac(mac, sizeof(mac), seen_cache[i].mac);
    snprintf(txt, sizeof(txt), "%s  %d", mac, (int)seen_cache[i].rssi);
    bool selected = g_settings.has_mac && memcmp(g_settings.mac, seen_cache[i].mac, 6) == 0;
    lv_obj_t *bi = lv_list_add_btn(tag_list, selected ? LV_SYMBOL_OK : LV_SYMBOL_BLUETOOTH, txt);
    lv_obj_add_event_cb(bi, tag_select_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
  }

  if (g_settings.has_mac) {
    fmt_mac(mac, sizeof(mac), g_settings.mac);
    snprintf(txt, sizeof(txt), "Using: %s", mac);
  } else {
    snprintf(txt, sizeof(txt), "Using: auto");
  }
  lv_label_set_text(tag_cur_lbl, txt);
}

static void tag_select_cb(lv_event_t *e) {
  intptr_t idx = (intptr_t)lv_event_get_user_data(e);
  if (idx < 0) ruuvi_select_mac(NULL);
  else if (idx < seen_n) ruuvi_select_mac(seen_cache[idx].mac);
  lv_async_call(refresh_tag_list, NULL);     // never rebuild a list inside its own button callback
}

static void tag_refresh_btn_cb(lv_event_t *e) {
  lv_async_call(refresh_tag_list, NULL);
}

static void tile_changed_cb(lv_event_t *e) {
  if (lv_tileview_get_tile_act(tv) == page_sensor) lv_async_call(refresh_tag_list, NULL);
}

// ---- periodic UI updates -------------------------------------------------
static void ui_update_cb(lv_timer_t *t) {
  RuuviReading r;
  bool have = ruuvi_get_reading(&r);
  char buf[64];

  if (!have) {
    set_text_if_changed(lbl_temp, "--.-°");
    set_color_if_changed(lbl_temp, COL_GREY);
    set_text_if_changed(lbl_hum_text, "Searching...");
    set_color_if_changed(lbl_hum_text, COL_GREY);
    if (lv_arc_get_value(arc_hum) != 0) lv_arc_set_value(arc_hum, 0);
    set_text_if_changed(lbl_pres, "---- hPa\n" LV_SYMBOL_BATTERY_EMPTY " -.-- V");
    return;
  }

  bool stale = r.age_ms > stale_limit_ms();

  if (r.temp_ok) snprintf(buf, sizeof(buf), "%.1f°", r.temp_c);
  else           snprintf(buf, sizeof(buf), "--.-°");
  set_text_if_changed(lbl_temp, buf);
  set_color_if_changed(lbl_temp, (stale || !r.temp_ok) ? COL_GREY : temp_color(r.temp_c));

  if (stale)          snprintf(buf, sizeof(buf), "No signal %um", (unsigned)(r.age_ms / 60000UL));
  else if (r.hum_ok)  snprintf(buf, sizeof(buf), "%d%% RH", (int)(r.hum_pct + 0.5f));
  else                snprintf(buf, sizeof(buf), "--%% RH");
  set_text_if_changed(lbl_hum_text, buf);
  set_color_if_changed(lbl_hum_text, stale ? COL_GREY : COL_ACCENT);

  int arc_val = (!stale && r.hum_ok) ? (int)(r.hum_pct + 0.5f) : 0;
  if (arc_val > 100) arc_val = 100;
  if (lv_arc_get_value(arc_hum) != arc_val) lv_arc_set_value(arc_hum, arc_val);

  char p[24], b[24];
  if (r.pres_ok) snprintf(p, sizeof(p), "%.1f hPa", r.pres_hpa);
  else           snprintf(p, sizeof(p), "---- hPa");
  if (r.batt_ok) snprintf(b, sizeof(b), "%s %.2f V", battery_symbol(r.batt_mv), r.batt_mv / 1000.0f);
  else           snprintf(b, sizeof(b), LV_SYMBOL_BATTERY_EMPTY " -.-- V");
  snprintf(buf, sizeof(buf), "%s\n%s", p, b);
  set_text_if_changed(lbl_pres, buf);
}

static void clock_cb(lv_timer_t *t) {
  struct tm tm;
  bool ok = wifi_get_time(&tm);
  char buf[8];
  if (ok) snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
  else    snprintf(buf, sizeof(buf), "--:--");
  set_text_if_changed(lbl_clock, buf);

  night_now = ok && is_night(tm.tm_hour);
  apply_brightness();

  set_text_if_changed(wifi_status_lbl, wifi_status_text());
}

// ---- page construction ---------------------------------------------------
static void make_title(lv_obj_t *parent, const char *text, int y) {
  lv_obj_t *title = lv_label_create(parent);
  lv_label_set_text(title, text);
  lv_obj_set_style_text_color(title, COL_TITLE, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, y);
}

static void create_page_main(lv_obj_t *parent) {
  lv_obj_set_style_bg_color(parent, COL_BG_MAIN, 0);
  lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
  lv_obj_add_event_cb(parent, gesture_event_cb, LV_EVENT_GESTURE, NULL);
  lv_obj_clear_flag(parent, LV_OBJ_FLAG_GESTURE_BUBBLE);

  // 270 degree humidity gauge with the gap at the bottom
  arc_hum = lv_arc_create(parent);
  lv_obj_set_size(arc_hum, 228, 228);
  lv_obj_center(arc_hum);
  lv_arc_set_rotation(arc_hum, 135);
  lv_arc_set_bg_angles(arc_hum, 0, 270);
  lv_arc_set_range(arc_hum, 0, 100);
  lv_arc_set_value(arc_hum, 0);
  lv_obj_remove_style(arc_hum, NULL, LV_PART_KNOB);
  lv_obj_clear_flag(arc_hum, LV_OBJ_FLAG_CLICKABLE);      // touching the gauge must not change it
  lv_obj_set_style_arc_width(arc_hum, 10, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc_hum, 10, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arc_hum, lv_color_make(0, 50, 100), LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc_hum, COL_ACCENT, LV_PART_INDICATOR);

  lbl_clock = lv_label_create(parent);
  lv_label_set_text(lbl_clock, "--:--");
  lv_obj_set_style_text_font(lbl_clock, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(lbl_clock, lv_color_make(180, 210, 255), 0);
  lv_obj_align(lbl_clock, LV_ALIGN_TOP_MID, 0, 48);

  lbl_temp = lv_label_create(parent);
  lv_label_set_text(lbl_temp, "--.-°");
  lv_obj_set_style_text_font(lbl_temp, &lv_font_montserrat_48, 0);
  lv_obj_set_style_text_color(lbl_temp, COL_GREY, 0);
  lv_obj_align(lbl_temp, LV_ALIGN_CENTER, 0, -8);

  lbl_hum_text = lv_label_create(parent);
  lv_label_set_text(lbl_hum_text, "Searching...");
  lv_obj_set_style_text_color(lbl_hum_text, COL_GREY, 0);
  lv_obj_align(lbl_hum_text, LV_ALIGN_CENTER, 0, 36);

  lv_obj_t *panel = lv_obj_create(parent);
  lv_obj_set_size(panel, 120, 48);
  lv_obj_align(panel, LV_ALIGN_BOTTOM_MID, 0, -22);
  lv_obj_set_style_bg_color(panel, COL_PANEL, 0);
  lv_obj_set_style_border_width(panel, 0, 0);
  lv_obj_set_style_radius(panel, 15, 0);
  lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(panel, LV_OBJ_FLAG_CLICKABLE);        // let swipes reach the tile

  lbl_pres = lv_label_create(panel);
  lv_label_set_text(lbl_pres, "---- hPa\n" LV_SYMBOL_BATTERY_EMPTY " -.-- V");
  lv_obj_set_style_text_align(lbl_pres, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(lbl_pres, lv_color_make(200, 220, 255), 0);
  lv_obj_center(lbl_pres);
}

static void create_page_settings(lv_obj_t *parent) {
  lv_obj_set_style_bg_color(parent, COL_BG_PAGE, 0);
  lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
  make_title(parent, "Settings", 12);

  dd_interval = lv_dropdown_create(parent);
  lv_dropdown_set_options(dd_interval, "Scan every 5 s\nScan every 30 s\nScan every 1 min\nScan continuous");
  lv_obj_set_size(dd_interval, 150, 30);
  lv_obj_align(dd_interval, LV_ALIGN_TOP_MID, 0, 32);
  lv_obj_set_style_bg_color(dd_interval, COL_PANEL, 0);
  lv_obj_set_style_text_color(dd_interval, lv_color_white(), 0);
  for (int i = 0; i < 4; i++) {
    if (kIntervals[i] == g_settings.scan_interval_s) lv_dropdown_set_selected(dd_interval, i);
  }
  lv_obj_add_event_cb(dd_interval, dd_interval_cb, LV_EVENT_VALUE_CHANGED, NULL);

  lv_obj_t *night = lv_label_create(parent);
  lv_label_set_text(night, "Night dim (hours)");
  lv_obj_set_style_text_color(night, lv_color_make(180, 210, 255), 0);
  lv_obj_align(night, LV_ALIGN_TOP_MID, 0, 72);

  const char *hours = "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23";
  lv_style_init(&style_roller);
  lv_style_set_bg_color(&style_roller, COL_PANEL);
  lv_style_set_text_color(&style_roller, lv_color_white());

  roller_start = lv_roller_create(parent);
  lv_roller_set_options(roller_start, hours, LV_ROLLER_MODE_NORMAL);
  lv_roller_set_visible_row_count(roller_start, 2);
  lv_obj_set_width(roller_start, 56);
  lv_obj_align(roller_start, LV_ALIGN_TOP_MID, -42, 92);
  lv_obj_add_style(roller_start, &style_roller, 0);
  lv_roller_set_selected(roller_start, g_settings.night_start, LV_ANIM_OFF);
  lv_obj_add_event_cb(roller_start, roller_cb, LV_EVENT_VALUE_CHANGED, NULL);

  roller_end = lv_roller_create(parent);
  lv_roller_set_options(roller_end, hours, LV_ROLLER_MODE_NORMAL);
  lv_roller_set_visible_row_count(roller_end, 2);
  lv_obj_set_width(roller_end, 56);
  lv_obj_align(roller_end, LV_ALIGN_TOP_MID, 42, 92);
  lv_obj_add_style(roller_end, &style_roller, 0);
  lv_roller_set_selected(roller_end, g_settings.night_end, LV_ANIM_OFF);
  lv_obj_add_event_cb(roller_end, roller_cb, LV_EVENT_VALUE_CHANGED, NULL);

  lv_obj_t *arrow = lv_label_create(parent);
  lv_label_set_text(arrow, LV_SYMBOL_RIGHT);
  lv_obj_set_style_text_color(arrow, lv_color_white(), 0);
  lv_obj_align(arrow, LV_ALIGN_TOP_MID, 0, 108);

  lv_obj_t *lbl_sleep = lv_label_create(parent);
  lv_label_set_text(lbl_sleep, "Wi-Fi sleep");
  lv_obj_set_style_text_color(lbl_sleep, lv_color_white(), 0);
  lv_obj_align(lbl_sleep, LV_ALIGN_TOP_MID, -38, 164);

  sw_wifi_sleep = lv_switch_create(parent);
  lv_obj_set_size(sw_wifi_sleep, 46, 24);
  lv_obj_align(sw_wifi_sleep, LV_ALIGN_TOP_MID, 42, 160);
  if (g_settings.wifi_auto_off) lv_obj_add_state(sw_wifi_sleep, LV_STATE_CHECKED);
  lv_obj_add_event_cb(sw_wifi_sleep, sw_sleep_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

static void create_page_wifi(lv_obj_t *parent) {
  lv_obj_set_style_bg_color(parent, COL_BG_PAGE, 0);
  lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
  make_title(parent, "Wi-Fi", 12);

  wifi_status_lbl = lv_label_create(parent);
  lv_label_set_long_mode(wifi_status_lbl, LV_LABEL_LONG_DOT);
  lv_obj_set_width(wifi_status_lbl, 150);
  lv_label_set_text(wifi_status_lbl, "");
  lv_obj_set_style_text_align(wifi_status_lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(wifi_status_lbl, lv_color_make(180, 210, 255), 0);
  lv_obj_align(wifi_status_lbl, LV_ALIGN_TOP_MID, 0, 34);

  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_size(btn, 110, 28);
  lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 56);
  lv_obj_set_style_bg_color(btn, lv_color_make(0, 100, 200), 0);
  btn_scan_lbl = lv_label_create(btn);
  lv_label_set_text(btn_scan_lbl, "Scan Wi-Fi");
  lv_obj_center(btn_scan_lbl);
  lv_obj_add_event_cb(btn, scan_wifi_event_cb, LV_EVENT_CLICKED, NULL);

  wifi_list = lv_list_create(parent);
  lv_obj_set_size(wifi_list, 170, 98);
  lv_obj_align(wifi_list, LV_ALIGN_TOP_MID, 0, 92);
  lv_obj_set_style_bg_color(wifi_list, COL_BG_MAIN, 0);
}

static void create_page_sensor(lv_obj_t *parent) {
  lv_obj_set_style_bg_color(parent, COL_BG_PAGE, 0);
  lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
  make_title(parent, "Sensor", 12);

  tag_cur_lbl = lv_label_create(parent);
  lv_label_set_long_mode(tag_cur_lbl, LV_LABEL_LONG_DOT);
  lv_obj_set_width(tag_cur_lbl, 150);
  lv_label_set_text(tag_cur_lbl, "Using: auto");
  lv_obj_set_style_text_align(tag_cur_lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(tag_cur_lbl, lv_color_make(180, 210, 255), 0);
  lv_obj_align(tag_cur_lbl, LV_ALIGN_TOP_MID, 0, 34);

  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_size(btn, 110, 28);
  lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 56);
  lv_obj_set_style_bg_color(btn, lv_color_make(0, 100, 200), 0);
  lv_obj_t *l = lv_label_create(btn);
  lv_label_set_text(l, LV_SYMBOL_REFRESH " Refresh");
  lv_obj_center(l);
  lv_obj_add_event_cb(btn, tag_refresh_btn_cb, LV_EVENT_CLICKED, NULL);

  tag_list = lv_list_create(parent);
  lv_obj_set_size(tag_list, 176, 98);
  lv_obj_align(tag_list, LV_ALIGN_TOP_MID, 0, 92);
  lv_obj_set_style_bg_color(tag_list, COL_BG_MAIN, 0);
}

void gui_init_ruuvi_hub(void) {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

  tv = lv_tileview_create(scr);
  lv_obj_set_scrollbar_mode(tv, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_event_cb(tv, tile_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

  page_main     = lv_tileview_add_tile(tv, 0, 0, LV_DIR_RIGHT);
  page_settings = lv_tileview_add_tile(tv, 1, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
  page_wifi     = lv_tileview_add_tile(tv, 2, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
  page_sensor   = lv_tileview_add_tile(tv, 3, 0, LV_DIR_LEFT);

  create_page_main(page_main);
  create_page_settings(page_settings);
  create_page_wifi(page_wifi);
  create_page_sensor(page_sensor);

  lv_timer_create(ui_update_cb, 500, NULL);
  lv_timer_create(clock_cb, 1000, NULL);
  lv_timer_create(wifi_poll_cb, 300, NULL);

  ui_update_cb(NULL);
  clock_cb(NULL);
}
