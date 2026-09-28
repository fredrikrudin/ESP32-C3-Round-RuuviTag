#pragma once
#include <stdint.h>
#include <stddef.h>

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

// Latest reading of ONE specific tag (thread-safe). False if that tag has not been heard.
bool ruuvi_get_reading(const uint8_t* mac, RuuviReading* out);

// RuuviTags heard recently, sorted by MAC (stable order). Returns the count.
int  ruuvi_get_seen(RuuviSeen* out, int max);

// "Ruuvi 2233" - same short name the RuuviTag advertises (last two MAC bytes).
void ruuvi_format_name(const uint8_t* mac, char* out, size_t n);
