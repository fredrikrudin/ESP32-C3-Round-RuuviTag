#pragma once
#include <stdint.h>

// All user-changeable settings, persisted in NVS (namespace "ruuvi_cfg").
struct Settings {
  int     scan_interval_s = 5;      // 0 = continuous, otherwise 5 / 30 / 60
  uint8_t night_start     = 22;     // hour 0..23
  uint8_t night_end       = 6;      // hour 0..23
  uint8_t brightness      = 150;    // user brightness 10..255
  bool    wifi_auto_off   = false;  // turn Wi-Fi off after NTP sync (less heat)
  bool    has_mac         = false;  // true = locked to one RuuviTag
  uint8_t mac[6]          = {0, 0, 0, 0, 0, 0};
};

extern Settings g_settings;

void settings_load();
void settings_save();
void settings_request_save();   // debounced: written ~3 s after the last request
void settings_tick();           // call from loop()
