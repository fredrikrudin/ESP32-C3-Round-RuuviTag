#pragma once
#include <Arduino.h>
#include <time.h>

void init_wifi_manager(void);
void tick_wifi_manager(void);

// Non-blocking network scan
bool   wifi_scan_start(void);
int    wifi_scan_poll(void);          // -1 running, -2 failed/not started, >=0 number of networks
String wifi_scan_ssid(int i);
int    wifi_scan_rssi(int i);
void   wifi_scan_finish(void);        // frees the scan results

// Credentials are only saved after the connection succeeded.
void        wifi_connect(const char* ssid, const char* password);
const char* wifi_status_text(void);

// Local time; returns false until NTP has synced. Never blocks.
bool wifi_get_time(struct tm* out);
