#include "settings.h"
#include <Arduino.h>
#include <Preferences.h>

Settings g_settings;

static Preferences prefs;
static bool     dirty    = false;
static uint32_t dirty_at = 0;

static void sanitize() {
  int s = g_settings.scan_interval_s;
  if (s != 0 && s != 5 && s != 30 && s != 60) g_settings.scan_interval_s = 5;
  if (g_settings.night_start > 23) g_settings.night_start = 22;
  if (g_settings.night_end > 23)   g_settings.night_end = 6;
  if (g_settings.brightness < 10)  g_settings.brightness = 10;
}

void settings_load() {
  g_settings = Settings();
  if (prefs.begin("ruuvi_cfg", false)) {
    g_settings.scan_interval_s = prefs.getInt("scan", g_settings.scan_interval_s);
    g_settings.night_start     = prefs.getUChar("ns", g_settings.night_start);
    g_settings.night_end       = prefs.getUChar("ne", g_settings.night_end);
    g_settings.brightness      = prefs.getUChar("bri", g_settings.brightness);
    g_settings.wifi_auto_off   = prefs.getBool("wsleep", false);
    g_settings.has_mac         = prefs.getBool("hasmac", false);
    if (g_settings.has_mac && prefs.getBytesLength("mac") == 6) {
      prefs.getBytes("mac", g_settings.mac, 6);
    } else {
      g_settings.has_mac = false;
    }
    prefs.end();
  }
  sanitize();
}

void settings_save() {
  if (!prefs.begin("ruuvi_cfg", false)) return;
  prefs.putInt("scan", g_settings.scan_interval_s);
  prefs.putUChar("ns", g_settings.night_start);
  prefs.putUChar("ne", g_settings.night_end);
  prefs.putUChar("bri", g_settings.brightness);
  prefs.putBool("wsleep", g_settings.wifi_auto_off);
  prefs.putBool("hasmac", g_settings.has_mac);
  prefs.putBytes("mac", g_settings.mac, 6);
  prefs.end();
}

void settings_request_save() {
  dirty = true;
  dirty_at = millis();
}

void settings_tick() {
  if (dirty && millis() - dirty_at > 3000) {
    settings_save();
    dirty = false;
  }
}
