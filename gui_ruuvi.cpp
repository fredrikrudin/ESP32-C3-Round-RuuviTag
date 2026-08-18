#include <lvgl.h>
#include <Arduino.h>
#include "wifi_manager.h"

#define TFT_BL 3

// Global UI Objects (Accessed by other modules via extern)
lv_obj_t *lbl_temp;
lv_obj_t *lbl_hum_text;
lv_obj_t *lbl_pres;
lv_obj_t *lbl_clock;
lv_obj_t *arc_hum;
lv_obj_t *wifi_list;

static lv_obj_t *tv, *page_main, *page_scan, *wifi_ta_pass, *wifi_kb;
static lv_obj_t *dd_interval, *roller_start, *roller_slut;
static lv_style_t style_roller;
int current_brightness = 150;

extern volatile int scan_interval;
extern volatile int night_start_hour;
extern volatile int night_end_hour;

void set_display_brightness(int brightness) {
    if(brightness < 10) brightness = 10;
    if(brightness > 255) brightness = 255;
    current_brightness = brightness;
    analogWrite(TFT_BL, current_brightness);
}

static void gesture_event_cb(lv_event_t * e) {
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    if (dir == LV_DIR_TOP) set_display_brightness(current_brightness + 35);
    else if (dir == LV_DIR_BOTTOM) set_display_brightness(current_brightness - 35);
}

static void wifi_pass_ready_cb(lv_event_t * e) {
    if(lv_event_get_code(e) == LV_EVENT_READY) {
        const char * password = lv_textarea_get_text(wifi_ta_pass);
        connect_to_new_wifi(password);
        lv_obj_delete(wifi_ta_pass);
        lv_obj_delete(wifi_kb);
        wifi_kb = NULL;
    }
}

static void wifi_network_select_cb(lv_event_t * e) {
    save_selected_ssid(lv_list_get_button_text(wifi_list, lv_event_get_target(e)));
    
    wifi_ta_pass = lv_textarea_create(lv_screen_act());
    lv_textarea_set_password_mode(wifi_ta_pass, true);
    lv_obj_set_size(wifi_ta_pass, 200, 40);
    lv_obj_align(wifi_ta_pass, LV_ALIGN_TOP_MID, 0, 15);
    lv_obj_add_event_cb(wifi_ta_pass, wifi_pass_ready_cb, LV_EVENT_ALL, NULL);

    wifi_kb = lv_keyboard_create(lv_screen_act());
    lv_obj_set_size(wifi_kb, 240, 120);
    lv_obj_align(wifi_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(wifi_kb, wifi_ta_pass);
}

static void scan_wifi_event_cb(lv_event_t * e) {
    scan_and_populate_wifi();
}

static void dropdown_event_cb(lv_event_t * e) {
    char buf[32];
    lv_dropdown_get_selected_str(dd_interval, buf, sizeof(buf));
    if (strcmp(buf, "Continuous") == 0) scan_interval = 0;
    else if (strcmp(buf, "Every 5 sec") == 0) scan_interval = 5;
    else if (strcmp(buf, "Every 30 sec") == 0) scan_interval = 30;
    else if (strcmp(buf, "Every 1 min") == 0) scan_interval = 60;
}

static void roller_event_cb(lv_event_t * e) {
    lv_obj_t * roller = lv_event_get_target(e);
    uint16_t sel = lv_roller_get_selected(roller);
    if(roller == roller_start) night_start_hour = sel;
    else night_end_hour = sel;
}

void create_page_main(lv_obj_t * parent) {
    lv_obj_set_style_bg_color(parent, lv_color_make(5, 10, 25), 0);
    lv_obj_add_event_cb(parent, gesture_event_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_GESTURE_BUBBLE);

    arc_hum = lv_arc_create(parent);
    lv_obj_set_size(arc_hum, 220, 220);
    lv_obj_center(arc_hum);
    lv_arc_set_bg_angles(arc_hum, 180, 360);
    lv_arc_set_value(arc_hum, 0);
    lv_obj_remove_style(arc_hum, NULL, LV_PART_KNOB);
    lv_obj_set_style_arc_color(arc_hum, lv_color_make(0, 50, 100), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_hum, lv_color_make(0, 190, 255), LV_PART_INDICATOR);

    // Digital Clock Placement Header
    lbl_clock = lv_label_create(parent);
    lv_label_set_text(lbl_clock, "00:00");
    lv_obj_set_style_text_font(lbl_clock, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_clock, lv_color_make(180, 210, 255), 0);
    lv_obj_align(lbl_clock, LV_ALIGN_TOP_MID, 0, 45);

    lbl_hum_text = lv_label_create(parent);
    lv_label_set_text(lbl_hum_text, "--% RH");
    lv_obj_set_style_text_color(lbl_hum_text, lv_color_make(0, 190, 255), 0);
    lv_obj_align(lbl_hum_text, LV_ALIGN_TOP_MID, 0, 20);

    lbl_temp = lv_label_create(parent);
    lv_label_set_text(lbl_temp, "--.-°");
    lv_obj_set_style_text_font(lbl_temp, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(lbl_temp, lv_color_white(), 0);
    lv_obj_align(lbl_temp, LV_ALIGN_CENTER, 0, -5);

    lv_obj_t * bottom_panel = lv_obj_create(parent);
    lv_obj_set_size(bottom_panel, 160, 65);
    lv_obj_align(bottom_panel, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_color(bottom_panel, lv_color_make(15, 35, 75), 0);
    lv_obj_set_style_border_width(bottom_panel, 0, 0);
    lv_obj_set_style_radius(bottom_panel, 15, 0);

    lbl_pres = lv_label_create(bottom_panel);
    lv_label_set_text(lbl_pres, LV_SYMBOL_CHARGE " ---- hPa\n" LV_SYMBOL_BATTERY " --%");
    lv_obj_set_style_text_align(lbl_pres, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(lbl_pres, lv_color_make(200, 220, 255), 0);
    lv_obj_center(lbl_pres);
}

void create_page_scan(lv_obj_t * parent) {
    lv_obj_set_style_bg_color(parent, lv_color_make(10, 15, 35), 0);

    lv_obj_t * title = lv_label_create(parent);
    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_color(title, lv_color_make(0, 150, 255), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    dd_interval = lv_dropdown_create(parent);
    lv_dropdown_set_options(dd_interval, "Every 5 sec\nEvery 30 sec\nEvery 1 min\nContinuous");
    lv_obj_set_size(dd_interval, 140, 30);
    lv_obj_align(dd_interval, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_style_bg_color(dd_interval, lv_color_make(15, 35, 75), 0);
    lv_obj_set_style_text_color(dd_interval, lv_color_white(), 0);
    lv_obj_add_event_cb(dd_interval, dropdown_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    const char * hours = "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23";
    lv_style_init(&style_roller);
    lv_style_set_bg_color(&style_roller, lv_color_make(15, 35, 75));
    lv_style_set_text_color(&style_roller, lv_color_white());

    roller_start = lv_roller_create(parent);
    lv_roller_set_options(roller_start, hours, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(roller_start, 2);
    lv_obj_set_width(roller_start, 50);
    lv_obj_align(roller_start, LV_ALIGN_TOP_MID, -30, 85);
    lv_obj_add_style(roller_start, &style_roller, 0);
    lv_obj_add_event_cb(roller_start, roller_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    roller_slut = lv_roller_create(parent);
    lv_roller_set_options(roller_slut, hours, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(roller_slut, 2);
    lv_obj_set_width(roller_slut, 50);
    lv_obj_align(roller_slut, LV_ALIGN_TOP_MID, 30, 85);
    lv_obj_add_style(roller_slut, &style_roller, 0);
    lv_obj_add_event_cb(roller_slut, roller_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t * btn_scan = lv_button_create(parent);
    lv_obj_set_size(btn_scan, 100, 30);
    lv_obj_align(btn_scan, LV_ALIGN_TOP_MID, 0, 140);
    lv_obj_set_style_bg_color(btn_scan, lv_color_make(0, 100, 200), 0);
    lv_obj_t * lbl_scan = lv_label_create(btn_scan);
    lv_label_set_text(lbl_scan, "Scan Wi-Fi");
    lv_obj_center(lbl_scan);
    lv_obj_add_event_cb(btn_scan, scan_wifi_event_cb, LV_EVENT_CLICKED, NULL);

    wifi_list = lv_list_create(parent);
    lv_obj_set_size(wifi_list, 180, 60);
    lv_obj_align(wifi_list, LV_ALIGN_TOP_MID, 0, 175);
    lv_obj_set_style_bg_color(wifi_list, lv_color_make(5, 10, 25), 0);
}

void gui_init_ruuvi_hub(void) {
    pinMode(TFT_BL, OUTPUT);
    set_display_brightness(current_brightness);

    tv = lv_tileview_create(lv_screen_act());
    page_main = lv_tileview_add_tile(tv, 0, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
    create_page_main(page_main);

    page_scan = lv_tileview_add_tile(tv, 1, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
    create_page_scan(page_scan);
}
