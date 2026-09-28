#pragma once
#include <stdint.h>

#define RUUVI_MAX_SEEN 8

struct RuuviReading {
  float    temp_c;
  float    hum_pct;
  float    pres_hpa;
  uint16_t batt_mv;
  int8_t   rssi;
  uint8_t  mac[6];
  bool     temp_ok, hum_ok, pres_ok, batt_ok;   // false = sensor reported "invalid"
  uint32_t age_ms;                              // time since the packet was received
};

struct RuuviSeen {
  uint8_t  mac[6];
  int8_t   rssi;
  uint32_t age_ms;
};

void init_ruuvi_ble(void);
void tick_ruuvi_ble(void);

// Copies the latest reading (thread-safe). Returns false if nothing received yet.
bool ruuvi_get_reading(RuuviReading* out);

// Copies the list of RuuviTags heard recently (thread-safe). Returns the count.
int  ruuvi_get_seen(RuuviSeen* out, int max);

// Lock to one tag (mac = 6 bytes) or pass nullptr for "auto (strongest signal)".
void ruuvi_select_mac(const uint8_t* mac);
