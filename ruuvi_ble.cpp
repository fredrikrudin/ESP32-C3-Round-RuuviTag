#include "ruuvi_ble.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <string.h>
#include "config.h"
#include "settings.h"

// Written by the BLE host task, read by the main (LVGL) task -> guard with a spinlock.
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

static RuuviReading live;
static uint32_t     live_ms  = 0;
static bool         live_has = false;

// Tag selection
static bool    sel_has = false;
static uint8_t sel_mac[6];
static bool     auto_valid = false;
static uint8_t  auto_mac[6];
static int      auto_rssi = -127;
static uint32_t auto_ms = 0;

// Recently seen tags (for the selection UI)
struct SeenSlot { bool used; uint8_t mac[6]; int8_t rssi; uint32_t ms; };
static SeenSlot seen[RUUVI_MAX_SEEN];

static bool     continuous_running = false;
static uint32_t last_scan_ms  = 0;
static uint32_t last_clear_ms = 0;

// Must be called with the lock held.
static void seen_update(const uint8_t* mac, int rssi, uint32_t now) {
  int slot = -1, oldest = 0;
  for (int i = 0; i < RUUVI_MAX_SEEN; i++) {
    if (seen[i].used && memcmp(seen[i].mac, mac, 6) == 0) { slot = i; break; }
    if (!seen[i].used) { if (slot < 0) slot = i; }
    else if (seen[i].ms < seen[oldest].ms) oldest = i;
  }
  if (slot < 0) slot = oldest;
  seen[slot].used = true;
  memcpy(seen[slot].mac, mac, 6);
  seen[slot].rssi = (int8_t)rssi;
  seen[slot].ms = now;
}

// d = manufacturer data incl. 2-byte company id: 99 04 | 05 | payload... (26 bytes)
static void handle_format5(const uint8_t* d, int rssi) {
  RuuviReading r;
  memset(&r, 0, sizeof(r));

  int16_t  tRaw = (int16_t)(((uint16_t)d[3] << 8) | d[4]);
  uint16_t hRaw = ((uint16_t)d[5] << 8) | d[6];
  uint16_t pRaw = ((uint16_t)d[7] << 8) | d[8];
  uint16_t pwr  = ((uint16_t)d[15] << 8) | d[16];

  r.temp_ok = (tRaw != (int16_t)0x8000);
  r.hum_ok  = (hRaw != 0xFFFF);
  r.pres_ok = (pRaw != 0xFFFF);
  uint16_t bRaw = pwr >> 5;
  r.batt_ok = (bRaw != 0x7FF);

  r.temp_c   = tRaw * 0.005f;
  r.hum_pct  = hRaw * 0.0025f;
  r.pres_hpa = (pRaw + 50000) / 100.0f;
  r.batt_mv  = bRaw + 1600;
  r.rssi     = (int8_t)rssi;
  memcpy(r.mac, d + 20, 6);

  const uint8_t* mac = d + 20;
  uint32_t now = millis();

  portENTER_CRITICAL(&mux);
  seen_update(mac, rssi, now);

  bool accept;
  if (sel_has) {
    accept = (memcmp(mac, sel_mac, 6) == 0);
  } else {
    // Auto mode: follow the strongest tag, with hysteresis so we don't flip-flop.
    bool tracked = auto_valid && (now - auto_ms) < 30000;
    bool same    = auto_valid && memcmp(mac, auto_mac, 6) == 0;
    if (!tracked || same || rssi >= auto_rssi + 8) {
      memcpy(auto_mac, mac, 6);
      auto_rssi = rssi;
      auto_ms = now;
      auto_valid = true;
      accept = true;
    } else {
      accept = false;
    }
  }
  if (accept) {
    live = r;
    live_ms = now;
    live_has = true;
  }
  portEXIT_CRITICAL(&mux);
}

class RuuviCallbacks : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice* dev) override {
    std::string s = dev->getManufacturerData();
    if (s.length() < 26) return;
    const uint8_t* d = (const uint8_t*)s.data();
    // Company id 0x0499 is sent little-endian (99 04). Data format 5 = 0x05.
    if (d[0] != 0x99 || d[1] != 0x04 || d[2] != 0x05) return;
    handle_format5(d, dev->getRSSI());
  }
};

static RuuviCallbacks callbacks;

bool ruuvi_get_reading(RuuviReading* out) {
  bool has;
  portENTER_CRITICAL(&mux);
  has = live_has;
  if (has) {
    *out = live;
    out->age_ms = millis() - live_ms;
  }
  portEXIT_CRITICAL(&mux);
  return has;
}

int ruuvi_get_seen(RuuviSeen* out, int max) {
  int n = 0;
  uint32_t now = millis();
  portENTER_CRITICAL(&mux);
  for (int i = 0; i < RUUVI_MAX_SEEN && n < max; i++) {
    if (!seen[i].used) continue;
    memcpy(out[n].mac, seen[i].mac, 6);
    out[n].rssi = seen[i].rssi;
    out[n].age_ms = now - seen[i].ms;
    n++;
  }
  portEXIT_CRITICAL(&mux);
  return n;
}

void ruuvi_select_mac(const uint8_t* mac) {
  portENTER_CRITICAL(&mux);
  if (mac) { sel_has = true; memcpy(sel_mac, mac, 6); }
  else     { sel_has = false; }
  auto_valid = false;
  live_has = false;                 // drop data from the previously followed tag
  portEXIT_CRITICAL(&mux);

  g_settings.has_mac = (mac != nullptr);
  if (mac) memcpy(g_settings.mac, mac, 6);
  settings_request_save();
}

void init_ruuvi_ble(void) {
  sel_has = g_settings.has_mac;
  memcpy(sel_mac, g_settings.mac, 6);

  NimBLEDevice::init("");
  NimBLEScan* pScan = NimBLEDevice::getScan();
  // wantDuplicates = true: otherwise a tag is reported only once per scan and
  // "Continuous" mode would never update after the first packet.
  pScan->setAdvertisedDeviceCallbacks(&callbacks, true);
  pScan->setActiveScan(false);      // passive is enough: Ruuvi data is in the advertisement
  pScan->setInterval(200);          // units of 0.625 ms
  pScan->setWindow(150);
}

void tick_ruuvi_ble(void) {
  uint32_t now = millis();
  NimBLEScan* pScan = NimBLEDevice::getScan();
  int interval = g_settings.scan_interval_s;

  if (interval == 0) {
    // Continuous. Restart once a minute to flush the library's result list
    // (it would otherwise grow with every BLE device in range).
    if (!pScan->isScanning() || !continuous_running) {
      pScan->clearResults();
      pScan->start(0, nullptr, false);
      continuous_running = true;
      last_clear_ms = now;
    } else if (now - last_clear_ms > 60000UL) {
      pScan->stop();
      pScan->clearResults();
      pScan->start(0, nullptr, false);
      last_clear_ms = now;
    }
  } else {
    if (continuous_running) {                       // switched away from continuous
      pScan->stop();
      continuous_running = false;
    }
    if (!pScan->isScanning() && (now - last_scan_ms >= (uint32_t)interval * 1000UL)) {
      pScan->start(BLE_SCAN_WINDOW_S, nullptr, false);   // start() clears old results itself
      last_scan_ms = now;
    }
  }
}
