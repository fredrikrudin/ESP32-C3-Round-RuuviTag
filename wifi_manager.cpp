#include "wifi_manager.h"
#include <WiFi.h>
#include <Preferences.h>
#include <lvgl.h>
#include "time.h"

extern lv_obj_t *wifi_list;
extern lv_obj_t *lbl_clock;
void set_display_brightness(int brightness);

Preferences wifi_prefs;
static bool ntp_active = false;
static char current_ssid[64] = "";

volatile int night_start_hour = 22;
volatile int night_end_hour = 6;
extern int current_brightness;

void init_wifi_manager(void) {
    WiFi.mode(WIFI_STA);
    wifi_prefs.begin("ruuvi_hub", true);
    String saved_ssid = wifi_prefs.getString("wifi_ssid", "");
    String saved_pass = wifi_prefs.getString("wifi_pass", "");
    wifi_prefs.end();

    if (saved_ssid.length() > 0) {
        Serial.printf("Auto-connecting to saved Wi-Fi: %s\n", saved_ssid.c_str());
        WiFi.begin(saved_ssid.c_str(), saved_pass.c_str());
    }
}

void scan_and_populate_wifi(void) {
    lv_list_clean(wifi_list);
    Serial.println("Scanning networks...");
    int n = WiFi.scanNetworks();
    for (int i = 0; i < n; ++i) {
        lv_list_add_button(wifi_list, LV_SYMBOL_WIFI, WiFi.SSID(i).c_str());
    }
}

void save_selected_ssid(const char* ssid) {
    strncpy(current_ssid, ssid, sizeof(current_ssid));
}

void connect_to_new_wifi(const char* password) {
    wifi_prefs.begin("ruuvi_hub", false);
    wifi_prefs.putString("wifi_ssid", current_ssid);
    wifi_prefs.putString("wifi_pass", password);
    wifi_prefs.end();
    
    Serial.printf("Connecting to selected network: %s\n", current_ssid);
    WiFi.begin(current_ssid, password);
}

void tick_wifi_manager(void) {
    // Sync time once Wi-Fi connects successfully
    if (WiFi.status() == WL_CONNECTED && !ntp_active) {
        Serial.println("Wi-Fi connected! Fetching NTP time...");
        configTime(3600, 3600, "pool.ntp.org"); // GMT+1 with standard European DST
        ntp_active = true;
    }

    // Refresh layout values and evaluate dimming criteria every second
    static unsigned long last_sec = 0;
    if (millis() - last_sec >= 1000 && ntp_active) {
        last_sec = millis();
        struct tm timeinfo;
        if (getLocalTime(&timeinfo)) {
            // 1. Update Layout Clock Elements
            char clk_buf[16];
            sprintf(clk_buf, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
            lv_label_set_text(lbl_clock, clk_buf);

            // 2. Evaluate Auto-Dimming Logic Window
            int hr = timeinfo.tm_hour;
            bool is_night = (night_start_hour > night_end_hour) ? 
                            (hr >= night_start_hour || hr < night_end_hour) : 
                            (hr >= night_start_hour && hr < night_end_hour);

            if (is_night) set_display_brightness(15); // Low profile ambient light
            else if (current_brightness == 15) set_display_brightness(150); // Restore standard brightness
        }
    }
}
