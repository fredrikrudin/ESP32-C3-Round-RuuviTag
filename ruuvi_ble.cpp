#include "ruuvi_ble.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <string.h>
#include "config.h"
#include "settings.h"

// Written by the BLE host task, read by the main (LVGL) task -> guard with a spinlock.
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

// Latest reading of every RuuviTag heard (so the UI can flip between sensors instantly).
struct Slot { bool used; RuuviReading r; uint32_t ms; };
static Slot slots[RUUVI_MAX_SEEN];

static bool     continuous_running = false;
static uint32_t last_scan_ms  = 0;
static uint32_t last_clear_ms = 0;

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

  uint32_t now = millis();

  portENTER_CRITICAL(&mux);
  int idx = -1, oldest = 0;
  for (int i = 0; i < RUUVI_MAX_SEEN; i++) {
    if (slots[i].used && memcmp(slots[i].r.mac, r.mac, 6) == 0) { idx = i; break; }
    if (!slots[i].used) { if (idx < 0) idx = i; }
    else if (slots[i].ms < slots[oldest].ms) oldest = i;
  }
  if (idx < 0) idx = oldest;        // table full: replace the tag heard longest ago
  slots[idx].used = true;
  slots[idx].r = r;
  slots[idx].ms = now;
  portEXIT_CRITICAL(&mux);
}

// NimBLE-Arduino 2.x API (NimBLEScanCallbacks, const device pointer)
class RuuviCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* dev) override {
    auto md = dev->getManufacturerData();          // company id (LE) + payload
    if (md.size() < 26) return;
    const uint8_t* d = (const uint8_t*)md.data();
    // Company id 0x0499 is sent little-endian (99 04). Data format 5 = 0x05.
    if (d[0] != 0x99 || d[1] != 0x04 || d[2] != 0x05) return;
    handle_format5(d, dev->getRSSI());
  }
};

static RuuviCallbacks callbacks;

bool ruuvi_get_reading(const uint8_t* mac, RuuviReading* out) {
  bool found = false;
  uint32_t now = millis();
  portENTER_CRITICAL(&mux);
  for (int i = 0; i < RUUVI_MAX_SEEN; i++) {
    if (slots[i].used && memcmp(slots[i].r.mac, mac, 6) == 0) {
      *out = slots[i].r;
      out->age_ms = now - slots[i].ms;
      found = true;
      break;
    }
  }
  portEXIT_CRITICAL(&mux);
  return found;
}

int ruuvi_get_seen(RuuviSeen* out, int max) {
  int n = 0;
  uint32_t now = millis();
  portENTER_CRITICAL(&mux);
  for (int i = 0; i < RUUVI_MAX_SEEN && n < max; i++) {
    if (!slots[i].used) continue;
    memcpy(out[n].mac, slots[i].r.mac, 6);
    out[n].rssi = slots[i].r.rssi;
    out[n].age_ms = now - slots[i].ms;
    n++;
  }
  portEXIT_CRITICAL(&mux);

  // insertion sort by MAC so the order never jumps around
  for (int i = 1; i < n; i++) {
    RuuviSeen key = out[i];
    int j = i - 1;
    while (j >= 0 && memcmp(out[j].mac, key.mac, 6) > 0) { out[j + 1] = out[j]; j--; }
    out[j + 1] = key;
  }
  return n;
}

void ruuvi_format_name(const uint8_t* mac, char* out, size_t n) {
  snprintf(out, n, "Ruuvi %02X%02X", mac[4], mac[5]);
}

void init_ruuvi_ble(void) {
  NimBLEDevice::init("");
  NimBLEScan* pScan = NimBLEDevice::getScan();
  // wantDuplicates = true: otherwise a tag is reported only once per scan and
  // "Continuous" mode would never update after the first packet.
  pScan->setScanCallbacks(&callbacks, true);
  pScan->setActiveScan(false);      // passive is enough: Ruuvi data is in the advertisement
  pScan->setInterval(125);          // NimBLE 2.x: milliseconds
  pScan->setWindow(94);
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
      pScan->start(0, false, true);          // 0 = scan until stopped
      continuous_running = true;
      last_clear_ms = now;
    } else if (now - last_clear_ms > 60000UL) {
      pScan->stop();
      pScan->clearResults();
      pScan->start(0, false, true);          // 0 = scan until stopped
      last_clear_ms = now;
    }
  } else {
    if (continuous_running) {                       // switched away from continuous
      pScan->stop();
      continuous_running = false;
    }
    if (!pScan->isScanning() && (now - last_scan_ms >= (uint32_t)interval * 1000UL)) {
      pScan->start(BLE_SCAN_WINDOW_S * 1000UL, false, true);   // milliseconds; clears old results itself
      last_scan_ms = now;
    }
  }
}
