#pragma once
// ---------------------------------------------------------------------------
//  Central configuration: pins, timings, time zone.
//  !! VERIFY THE PINS against your board's documentation / your working
//  !! ESP32-2424S012 sketch before flashing. ESP32-C3 only has GPIO0..GPIO21.
// ---------------------------------------------------------------------------

// Backlight (PWM). Do NOT also define TFT_BL in TFT_eSPI's User_Setup.h.
#define PIN_BACKLIGHT   3

// CST816S capacitive touch controller (I2C)
#define PIN_TOUCH_SDA   4
#define PIN_TOUCH_SCL   5
#define PIN_TOUCH_INT   0   // not used (polling), kept for reference
#define PIN_TOUCH_RST   1

// Time
#define RUUVI_TZ        "CET-1CEST,M3.5.0,M10.5.0/3"   // POSIX TZ string
#define NTP_SERVER_1    "pool.ntp.org"
#define NTP_SERVER_2    "time.google.com"

// Backlight behaviour
#define MIN_BRIGHTNESS    10
#define BRIGHTNESS_STEP   35
#define NIGHT_BRIGHTNESS  15
#define NIGHT_WAKE_MS     20000UL   // touch during night mode -> normal brightness for this long

// Navigation
#define LONG_PRESS_MS       700UL     // long press on the main screen opens the setup screens
#define SETTINGS_TIMEOUT_MS 60000UL   // setup screens return to the main screen after this idle time
#define VIEW_REVERT_MS      60000UL   // main screen returns to the default sensor after this idle time (0 = never)

// BLE
#define BLE_SCAN_WINDOW_S 3         // seconds per scan burst in periodic mode

// Wi-Fi
#define WIFI_RESYNC_MS    86400000UL  // re-sync NTP every 24 h when "Wi-Fi sleep" is on
