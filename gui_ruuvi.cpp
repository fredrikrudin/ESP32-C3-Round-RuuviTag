// LVGL 8.3.x UI for a 240x240 round display.
//
//  MAIN SCREEN   swipe left/right = previous/next sensor, swipe up/down = brightness,
//                long press = open the setup screens.
//  SETUP SCREENS Settings | Wi-Fi | Sensor  (swipe or tap < >), "Back" returns to the main
//                screen (also automatically after SETTINGS_TIMEOUT_MS without a touch).
//
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

#define NUM_SETUP_PAGES 3

#define COL_BG_MAIN   lv_color_make(5, 10, 25)
#define COL_BG_PAGE   lv_color_make(10, 15, 35)
#define COL_PANEL     lv_color_make(15, 35, 75)
#define COL_ACCENT    lv_color_make(0, 190, 255)
#define COL_TITLE     lv_color_make(0, 150, 255)
#define COL_SOFT      lv_color_make(180, 210, 255)
#define COL_GREY      lv_color_make(90, 100, 120)
#define COL_DIM       lv_color_make(120, 140, 180)
#define COL_WIFI_ON   lv_color_make(140, 150, 170)   // dim grey: connected / lit bar
#define COL_WIFI_OFF  lv_color_make(45, 52, 68)      // unlit bar / disconnected symbol

// ---- screens and widgets ---------------------------------------------------
static lv_obj_t *scr_main, *scr_setup;
static lv_obj_t *tv_setup, *page_settings, *page_wifi, *page_sensor;

static lv_obj_t *arc_hum, *lbl_temp, *lbl_hum_text, *lbl_pres, *lbl_clock, *lbl_sensor;
static lv_obj_t *wifi_sym, *wifi_bar[4];

static lv_obj_t *dd_interval, *roller_start, *roller_end, *sw_wifi_sleep;

static lv_obj_t *wifi_status_lbl, *wifi_list, *btn_scan_lbl;
static lv_obj_t *wifi_modal, *wifi_ta, *wifi_kb;

static lv_obj_t *tag_cur_lbl, *tag_list;

static lv_style_t style_roller;

// ---- state -----------------------------------------------------------------
static uint32_t night_override_until = 0;
static bool     night_now = false;

#define MAX_NETS 12
static char ssid_cache[MAX_NETS][33];
static int  ssid_count = 0;
static bool wifi_scanning = false;
static char sel_ssid[33];

static RuuviSeen seen_cache[RUUVI_MAX_SEEN + 1];   // +1: the default tag if it is not currently heard
static int       seen_n = 0;

// Which sensor the main screen shows
static bool    view_auto = true;          // true = follow the strongest tag
static uint8_t view_mac[6];               // used when view_auto == false
static bool    auto_valid = false;
static uint8_t auto_mac[6];               // tag currently followed in auto mode

static const int kIntervals[] = {5, 30, 60, 0};

static void update_wifi_indicator();
static void ui_update_cb(lv_timer_t *t);
static void gui_refresh_tags(void);
static void refresh_tags_async(void *unused);
static void close_wifi_modal();

// ---- helpers ---------------------------------------------------------------
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

// ---- brightness / night mode -----------------------------------------------
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

// ---- which sensor is shown -------------------------------------------------
static void view_reset_to_default() {
  if (g_settings.has_mac) {
    view_auto = false;
    memcpy(view_mac, g_settings.mac, 6);
  } else {
    view_auto = true;
  }
}

// Auto mode: strongest tag heard recently, with hysteresis so the view does not flip-flop.
static bool pick_auto(const RuuviSeen *seen, int n, uint8_t *out) {
  int cur = -1, best = -1;
  for (int i = 0; i < n; i++) {
    if (seen[i].age_ms > 60000UL) continue;
    if (best < 0 || seen[i].rssi > seen[best].rssi) best = i;
    if (auto_valid && memcmp(seen[i].mac, auto_mac, 6) == 0) cur = i;
  }
  if (best < 0) return false;
  int pick = best;
  if (cur >= 0 && seen[cur].age_ms < 30000UL && seen[best].rssi < seen[cur].rssi + 8) pick = cur;
  memcpy(out, seen[pick].mac, 6);
  return true;
}

// dir = +1 next sensor, -1 previous sensor (wraps around)
static void sensor_step(int dir) {
  RuuviSeen sn[RUUVI_MAX_SEEN];
  int n = ruuvi_get_seen(sn, RUUVI_MAX_SEEN);

  uint8_t cyc[RUUVI_MAX_SEEN + 1][6];
  int m = 0;
  for (int i = 0; i < n; i++) memcpy(cyc[m++], sn[i].mac, 6);

  const uint8_t *cur = view_auto ? (auto_valid ? auto_mac : NULL) : view_mac;
  int idx = -1;
  if (cur) {
    for (int i = 0; i < m; i++) if (memcmp(cyc[i], cur, 6) == 0) idx = i;
    if (idx < 0) { memcpy(cyc[m], cur, 6); idx = m; m++; }   // shown tag not heard right now
  }
  if (m <= 1) return;                                        // nothing to flip to

  int next = (idx < 0) ? (dir > 0 ? 0 : m - 1) : (idx + dir + m) % m;
  uint8_t chosen[6];
  memcpy(chosen, cyc[next], 6);
  view_auto = false;
  memcpy(view_mac, chosen, 6);
  ui_update_cb(NULL);
}

static void step_cb(lv_event_t *e) {
  sensor_step((int)(intptr_t)lv_event_get_user_data(e));
}

// ---- footer helpers --------------------------------------------------------
static void make_arrow(lv_obj_t *parent, const char *txt, int x_off, lv_event_cb_t cb, void *ud) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, txt);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(l, COL_TITLE, 0);
  lv_obj_align(l, LV_ALIGN_BOTTOM_MID, x_off, -15);
  lv_obj_add_flag(l, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(l, 14);
  lv_obj_add_event_cb(l, cb, LV_EVENT_CLICKED, ud);
}

static lv_obj_t *make_footer_text(lv_obj_t *parent, const char *text, lv_color_t col,
                                  lv_event_cb_t cb) {
  lv_obj_t *m = lv_label_create(parent);
  lv_label_set_long_mode(m, LV_LABEL_LONG_DOT);
  lv_obj_set_width(m, 88);
  lv_label_set_text(m, text);
  lv_obj_set_style_text_align(m, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(m, col, 0);
  lv_obj_align(m, LV_ALIGN_BOTTOM_MID, 0, -16);
  if (cb) {
    lv_obj_add_flag(m, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(m, 10);
    lv_obj_add_event_cb(m, cb, LV_EVENT_CLICKED, NULL);
  }
  return m;
}

// ---- screen switching ------------------------------------------------------
static void open_setup() {
  lv_obj_set_tile_id(tv_setup, 0, 0, LV_ANIM_OFF);
  lv_scr_load_anim(scr_setup, LV_SCR_LOAD_ANIM_FADE_ON, 250, 0, false);
  lv_async_call(refresh_tags_async, NULL);
}

static void go_main() {
  if (wifi_modal) close_wifi_modal();
  lv_scr_load_anim(scr_main, LV_SCR_LOAD_ANIM_FADE_ON, 250, 0, false);
}

static void back_cb(lv_event_t *e) { go_main(); }

static void setup_nav_cb(lv_event_t *e) {
  intptr_t col = (intptr_t)lv_event_get_user_data(e);
  lv_obj_set_tile_id(tv_setup, (uint32_t)col, 0, LV_ANIM_ON);
}

// "<" previous page - "Back" - ">" next page
static void setup_footer(lv_obj_t *parent, int col) {
  if (col > 0)                   make_arrow(parent, "<", -54, setup_nav_cb, (void *)(intptr_t)(col - 1));
  if (col < NUM_SETUP_PAGES - 1) make_arrow(parent, ">",  54, setup_nav_cb, (void *)(intptr_t)(col + 1));
  make_footer_text(parent, "Back", COL_TITLE, back_cb);
}

// ---- main screen events ----------------------------------------------------
static void main_gesture_cb(lv_event_t *e) {
  lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
  if (dir == LV_DIR_LEFT)  { sensor_step(+1); return; }
  if (dir == LV_DIR_RIGHT) { sensor_step(-1); return; }

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

static void main_long_press_cb(lv_event_t *e) { open_setup(); }

// ---- settings page events --------------------------------------------------
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

// ---- wifi page -------------------------------------------------------------
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
    lv_obj_clean(wifi_list);
    lv_label_set_text(btn_scan_lbl, "Scanning...");
  } else {
    lv_label_set_text(btn_scan_lbl, "Scan failed");
  }
}

static void populate_wifi_list(int n) {
  lv_obj_clean(wifi_list);
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

// ---- sensor page: tick boxes, the ticked one is the default sensor ----------
static void add_tag_row(const char *text, bool checked, intptr_t idx);

static void tag_row_cb(lv_event_t *e) {
  intptr_t idx = (intptr_t)lv_event_get_user_data(e);
  if (idx < 0) {                                              // Auto (strongest)
    g_settings.has_mac = false;
  } else if (idx < seen_n) {
    g_settings.has_mac = true;
    memcpy(g_settings.mac, seen_cache[idx].mac, 6);
  }
  settings_request_save();
  view_reset_to_default();                                    // main screen follows the new default
  lv_async_call(refresh_tags_async, NULL);                    // never rebuild the list inside its own callback
}

static void add_tag_row(const char *text, bool checked, intptr_t idx) {
  lv_obj_t *cb = lv_checkbox_create(tag_list);
  lv_checkbox_set_text(cb, text);
  lv_obj_set_style_text_color(cb, lv_color_white(), 0);
  lv_obj_set_style_bg_color(cb, COL_PANEL, LV_PART_INDICATOR);
  lv_obj_set_style_border_color(cb, COL_ACCENT, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(cb, COL_ACCENT, LV_PART_INDICATOR | LV_STATE_CHECKED);
  if (checked) lv_obj_add_state(cb, LV_STATE_CHECKED);
  lv_obj_add_event_cb(cb, tag_row_cb, LV_EVENT_VALUE_CHANGED, (void *)idx);
}

// (Re)builds the tick-box list. Never call this from inside a row's own callback.
static void gui_refresh_tags(void) {
  char txt[40], nm[20];
  lv_obj_clean(tag_list);

  add_tag_row("Auto (strongest)", !g_settings.has_mac, -1);

  seen_n = ruuvi_get_seen(seen_cache, RUUVI_MAX_SEEN);

  // Make sure the default tag is always listed, even if it is not heard right now.
  if (g_settings.has_mac) {
    bool listed = false;
    for (int i = 0; i < seen_n; i++) {
      if (memcmp(seen_cache[i].mac, g_settings.mac, 6) == 0) { listed = true; break; }
    }
    if (!listed) {
      memcpy(seen_cache[seen_n].mac, g_settings.mac, 6);
      seen_cache[seen_n].rssi = 0;
      seen_cache[seen_n].age_ms = 0xFFFFFFFFUL;
      seen_n++;
    }
  }

  for (int i = 0; i < seen_n; i++) {
    ruuvi_format_name(seen_cache[i].mac, nm, sizeof(nm));
    if (seen_cache[i].age_ms == 0xFFFFFFFFUL) snprintf(txt, sizeof(txt), "%s (--)", nm);
    else snprintf(txt, sizeof(txt), "%s (%d)", nm, (int)seen_cache[i].rssi);
    bool selected = g_settings.has_mac && memcmp(g_settings.mac, seen_cache[i].mac, 6) == 0;
    add_tag_row(txt, selected, i);
  }

  if (g_settings.has_mac) {
    ruuvi_format_name(g_settings.mac, nm, sizeof(nm));
    snprintf(txt, sizeof(txt), "Default: %s", nm);
  } else {
    snprintf(txt, sizeof(txt), "Default: auto");
  }
  lv_label_set_text(tag_cur_lbl, txt);
}

static void refresh_tags_async(void *unused) { gui_refresh_tags(); }

static void tag_refresh_btn_cb(lv_event_t *e) {
  lv_async_call(refresh_tags_async, NULL);
}

static void tile_changed_cb(lv_event_t *e) {
  if (lv_tileview_get_tile_act(tv_setup) == page_sensor) lv_async_call(refresh_tags_async, NULL);
}

// ---- periodic UI updates ---------------------------------------------------
static void ui_update_cb(lv_timer_t *t) {
  // Decide which tag to show
  uint8_t target[6];
  bool have_target = false;
  if (view_auto) {
    RuuviSeen sn[RUUVI_MAX_SEEN];
    int n = ruuvi_get_seen(sn, RUUVI_MAX_SEEN);
    if (pick_auto(sn, n, target)) {
      have_target = true;
      memcpy(auto_mac, target, 6);
      auto_valid = true;
    }
  } else {
    memcpy(target, view_mac, 6);
    have_target = true;
  }

  RuuviReading r;
  bool have = have_target && ruuvi_get_reading(target, &r);
  bool stale = have && r.age_ms > stale_limit_ms();
  char buf[64];

  // Footer: which sensor is shown
  char nm[24];
  if (have && view_auto)         snprintf(nm, sizeof(nm), "Auto: %02X%02X", r.mac[4], r.mac[5]);
  else if (have)                 ruuvi_format_name(r.mac, nm, sizeof(nm));
  else if (!view_auto)           ruuvi_format_name(view_mac, nm, sizeof(nm));
  else                           snprintf(nm, sizeof(nm), "Searching");
  set_text_if_changed(lbl_sensor, nm);
  set_color_if_changed(lbl_sensor, (have && !stale) ? COL_SOFT : COL_GREY);

  if (!have) {
    set_text_if_changed(lbl_temp, "--.-°");
    set_color_if_changed(lbl_temp, COL_GREY);
    set_text_if_changed(lbl_hum_text, "Searching...");
    set_color_if_changed(lbl_hum_text, COL_GREY);
    if (lv_arc_get_value(arc_hum) != 0) lv_arc_set_value(arc_hum, 0);
    set_text_if_changed(lbl_pres, "---- hPa\n" LV_SYMBOL_BATTERY_EMPTY " -.-- V");
    return;
  }

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
  update_wifi_indicator();

  // Idle handling (touch based): leave setup, return to the default sensor
  uint32_t idle = lv_disp_get_inactive_time(NULL);
  if (lv_scr_act() == scr_setup && !wifi_modal && idle > SETTINGS_TIMEOUT_MS) go_main();
  if (VIEW_REVERT_MS && idle > VIEW_REVERT_MS) view_reset_to_default();
}

// ---- Wi-Fi symbol + 4 signal bars (dim grey), shown under the clock ----------
static int wifi_level_from_dbm(int dbm) {
  if (dbm == 0)   return -1;      // not connected
  if (dbm >= -55) return 4;
  if (dbm >= -65) return 3;
  if (dbm >= -75) return 2;
  return 1;
}

static void update_wifi_indicator() {
  static int last_level = -2;
  int level = wifi_level_from_dbm(wifi_signal_dbm());
  if (level == last_level) return;
  last_level = level;
  set_color_if_changed(wifi_sym, level >= 0 ? COL_WIFI_ON : COL_WIFI_OFF);
  for (int i = 0; i < 4; i++) {
    lv_obj_set_style_bg_color(wifi_bar[i], (i < level) ? COL_WIFI_ON : COL_WIFI_OFF, 0);
  }
}

static void create_wifi_indicator(lv_obj_t *parent) {
  lv_obj_t *box = lv_obj_create(parent);
  lv_obj_set_size(box, 38, 14);
  lv_obj_align(box, LV_ALIGN_TOP_MID, 0, 57);
  lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(box, 0, 0);
  lv_obj_set_style_pad_all(box, 0, 0);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_CLICKABLE);          // let swipes reach the screen

  wifi_sym = lv_label_create(box);
  lv_label_set_text(wifi_sym, LV_SYMBOL_WIFI);
  lv_obj_set_style_text_color(wifi_sym, COL_WIFI_OFF, 0);
  lv_obj_align(wifi_sym, LV_ALIGN_LEFT_MID, 0, 0);

  static const int heights[4] = {4, 7, 10, 13};
  for (int i = 0; i < 4; i++) {
    wifi_bar[i] = lv_obj_create(box);
    lv_obj_set_size(wifi_bar[i], 3, heights[i]);
    lv_obj_align(wifi_bar[i], LV_ALIGN_BOTTOM_LEFT, 19 + i * 5, 0);
    lv_obj_set_style_bg_color(wifi_bar[i], COL_WIFI_OFF, 0);
    lv_obj_set_style_bg_opa(wifi_bar[i], LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(wifi_bar[i], 0, 0);
    lv_obj_set_style_radius(wifi_bar[i], 1, 0);
    lv_obj_clear_flag(wifi_bar[i], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(wifi_bar[i], LV_OBJ_FLAG_CLICKABLE);
  }
}

// ---- screen construction ---------------------------------------------------
static void make_title(lv_obj_t *parent, const char *text, int y) {
  lv_obj_t *title = lv_label_create(parent);
  lv_label_set_text(title, text);
  lv_obj_set_style_text_color(title, COL_TITLE, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, y);
}

static void create_screen_main() {
  scr_main = lv_obj_create(NULL);
  lv_obj_t *parent = scr_main;
  lv_obj_set_style_bg_color(parent, COL_BG_MAIN, 0);
  lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
  lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(parent, main_gesture_cb, LV_EVENT_GESTURE, NULL);
  lv_obj_add_event_cb(parent, main_long_press_cb, LV_EVENT_LONG_PRESSED, NULL);

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
  lv_obj_set_style_text_color(lbl_clock, COL_SOFT, 0);
  lv_obj_align(lbl_clock, LV_ALIGN_TOP_MID, 0, 36);

  create_wifi_indicator(parent);

  lbl_temp = lv_label_create(parent);
  lv_label_set_text(lbl_temp, "--.-°");
  lv_obj_set_style_text_font(lbl_temp, &lv_font_montserrat_48, 0);
  lv_obj_set_style_text_color(lbl_temp, COL_GREY, 0);
  lv_obj_align(lbl_temp, LV_ALIGN_CENTER, 0, -24);

  lbl_hum_text = lv_label_create(parent);
  lv_label_set_text(lbl_hum_text, "Searching...");
  lv_obj_set_style_text_color(lbl_hum_text, COL_GREY, 0);
  lv_obj_align(lbl_hum_text, LV_ALIGN_CENTER, 0, 18);

  lv_obj_t *panel = lv_obj_create(parent);
  lv_obj_set_size(panel, 120, 44);
  lv_obj_align(panel, LV_ALIGN_BOTTOM_MID, 0, -42);
  lv_obj_set_style_bg_color(panel, COL_PANEL, 0);
  lv_obj_set_style_border_width(panel, 0, 0);
  lv_obj_set_style_pad_all(panel, 0, 0);
  lv_obj_set_style_radius(panel, 15, 0);
  lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(panel, LV_OBJ_FLAG_CLICKABLE);        // let swipes / long press reach the screen

  lbl_pres = lv_label_create(panel);
  lv_label_set_text(lbl_pres, "---- hPa\n" LV_SYMBOL_BATTERY_EMPTY " -.-- V");
  lv_obj_set_style_text_align(lbl_pres, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(lbl_pres, lv_color_make(200, 220, 255), 0);
  lv_obj_center(lbl_pres);

  // Footer: "<" previous sensor - name of the sensor shown - ">" next sensor
  make_arrow(parent, "<", -54, step_cb, (void *)(intptr_t)-1);
  make_arrow(parent, ">",  54, step_cb, (void *)(intptr_t)+1);
  lbl_sensor = make_footer_text(parent, "Searching", COL_SOFT, NULL);
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
  lv_obj_set_style_text_color(night, COL_SOFT, 0);
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

  setup_footer(parent, 0);
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
  lv_obj_set_style_text_color(wifi_status_lbl, COL_SOFT, 0);
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

  setup_footer(parent, 1);
}

static void create_page_sensor(lv_obj_t *parent) {
  lv_obj_set_style_bg_color(parent, COL_BG_PAGE, 0);
  lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
  make_title(parent, "Sensor", 12);

  tag_cur_lbl = lv_label_create(parent);
  lv_label_set_long_mode(tag_cur_lbl, LV_LABEL_LONG_DOT);
  lv_obj_set_width(tag_cur_lbl, 150);
  lv_label_set_text(tag_cur_lbl, "Default: auto");
  lv_obj_set_style_text_align(tag_cur_lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(tag_cur_lbl, COL_SOFT, 0);
  lv_obj_align(tag_cur_lbl, LV_ALIGN_TOP_MID, 0, 34);

  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_size(btn, 110, 28);
  lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 56);
  lv_obj_set_style_bg_color(btn, lv_color_make(0, 100, 200), 0);
  lv_obj_t *l = lv_label_create(btn);
  lv_label_set_text(l, LV_SYMBOL_REFRESH " Refresh");
  lv_obj_center(l);
  lv_obj_add_event_cb(btn, tag_refresh_btn_cb, LV_EVENT_CLICKED, NULL);

  // Scrollable column of tick boxes; the ticked row is the default sensor
  tag_list = lv_obj_create(parent);
  lv_obj_set_size(tag_list, 176, 98);
  lv_obj_align(tag_list, LV_ALIGN_TOP_MID, 0, 92);
  lv_obj_set_flex_flow(tag_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_bg_color(tag_list, COL_BG_MAIN, 0);
  lv_obj_set_style_border_width(tag_list, 0, 0);
  lv_obj_set_style_radius(tag_list, 4, 0);
  lv_obj_set_style_pad_all(tag_list, 6, 0);
  lv_obj_set_style_pad_row(tag_list, 8, 0);

  setup_footer(parent, 2);
}

static void create_screen_setup() {
  scr_setup = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_setup, lv_color_black(), 0);
  lv_obj_clear_flag(scr_setup, LV_OBJ_FLAG_SCROLLABLE);

  tv_setup = lv_tileview_create(scr_setup);
  lv_obj_set_scrollbar_mode(tv_setup, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_event_cb(tv_setup, tile_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

  page_settings = lv_tileview_add_tile(tv_setup, 0, 0, LV_DIR_RIGHT);
  page_wifi     = lv_tileview_add_tile(tv_setup, 1, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
  page_sensor   = lv_tileview_add_tile(tv_setup, 2, 0, LV_DIR_LEFT);

  create_page_settings(page_settings);
  create_page_wifi(page_wifi);
  create_page_sensor(page_sensor);
}

void gui_init_ruuvi_hub(void) {
  view_reset_to_default();

  create_screen_main();
  create_screen_setup();
  lv_scr_load(scr_main);

  lv_timer_create(ui_update_cb, 500, NULL);
  lv_timer_create(clock_cb, 1000, NULL);
  lv_timer_create(wifi_poll_cb, 300, NULL);

  gui_refresh_tags();
  ui_update_cb(NULL);
  clock_cb(NULL);
}
