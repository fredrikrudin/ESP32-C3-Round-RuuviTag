#include "wifi_manager.h"
#include <WiFi.h>
#include <Preferences.h>
#include <string.h>
#include "config.h"
#include "settings.h"

static Preferences wifi_prefs;      // namespace/keys unchanged -> old credentials keep working

static char saved_ssid[33] = "";
static char saved_pass[65] = "";
static char pend_ssid[33]  = "";
static char pend_pass[65]  = "";
static bool     pending = false;
static bool     failed  = false;
static uint32_t pend_started = 0;

static bool     ntp_started = false;
static uint32_t connected_since = 0;

static bool     asleep = false;
static uint32_t asleep_since = 0;
static uint32_t hold_until = 0;     // no auto-sleep until then (user is busy in the Wi-Fi menu)

static inline bool held() { return (int32_t)(hold_until - millis()) > 0; }

static void begin_saved() {
  if (saved_ssid[0]) WiFi.begin(saved_ssid, saved_pass[0] ? saved_pass : NULL);
}

static void wake_wifi() {
  asleep = false;
  failed = false;
  WiFi.mode(WIFI_STA);
  begin_saved();
}

void init_wifi_manager(void) {
  WiFi.persistent(false);           // we store credentials ourselves
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);

  if (wifi_prefs.begin("ruuvi_hub", false)) {
    wifi_prefs.getString("wifi_ssid", "").toCharArray(saved_ssid, sizeof(saved_ssid));
    wifi_prefs.getString("wifi_pass", "").toCharArray(saved_pass, sizeof(saved_pass));
    wifi_prefs.end();
  }
  if (saved_ssid[0]) {
    Serial.printf("Auto-connecting to saved Wi-Fi: %s\n", saved_ssid);
    begin_saved();
  }
}

bool wifi_scan_start(void) {
  hold_until = millis() + 120000UL;
  if (asleep) wake_wifi();
  WiFi.scanDelete();
  return WiFi.scanNetworks(true, false) != WIFI_SCAN_FAILED;   // async
}

int wifi_scan_poll(void)          { return WiFi.scanComplete(); }
String wifi_scan_ssid(int i)      { return WiFi.SSID(i); }
int wifi_scan_rssi(int i)         { return WiFi.RSSI(i); }
void wifi_scan_finish(void)       { WiFi.scanDelete(); }

void wifi_connect(const char* ssid, const char* password) {
  strlcpy(pend_ssid, ssid, sizeof(pend_ssid));
  strlcpy(pend_pass, password ? password : "", sizeof(pend_pass));
  pending = true;
  failed = false;
  pend_started = millis();
  hold_until = millis() + 120000UL;

  if (asleep) { asleep = false; WiFi.mode(WIFI_STA); }
  Serial.printf("Connecting to selected network: %s\n", pend_ssid);
  WiFi.disconnect();
  WiFi.begin(pend_ssid, pend_pass[0] ? pend_pass : NULL);
}

bool wifi_get_time(struct tm* out) {
  return getLocalTime(out, 0);      // 0 ms timeout: never block the UI
}

void tick_wifi_manager(void) {
  uint32_t now = millis();

  if (asleep) {
    if (!g_settings.wifi_auto_off || now - asleep_since >= WIFI_RESYNC_MS) wake_wifi();
    return;
  }

  bool connected = (WiFi.status() == WL_CONNECTED);

  // Save credentials only once the new network really works.
  if (pending) {
    if (connected && strcmp(WiFi.SSID().c_str(), pend_ssid) == 0) {
      if (wifi_prefs.begin("ruuvi_hub", false)) {
        wifi_prefs.putString("wifi_ssid", pend_ssid);
        wifi_prefs.putString("wifi_pass", pend_pass);
        wifi_prefs.end();
      }
      strlcpy(saved_ssid, pend_ssid, sizeof(saved_ssid));
      strlcpy(saved_pass, pend_pass, sizeof(saved_pass));
      pending = false;
      failed = false;
    } else if (WiFi.status() == WL_CONNECT_FAILED || now - pend_started > 20000UL) {
      pending = false;
      failed = true;
      WiFi.disconnect();
      begin_saved();                // fall back to the previously working network
    }
  }

  if (connected) {
    if (!ntp_started) {
      Serial.println("Wi-Fi connected, starting NTP");
      configTzTime(RUUVI_TZ, NTP_SERVER_1, NTP_SERVER_2);
      ntp_started = true;
      connected_since = now;
    }
  } else {
    ntp_started = false;
  }

  // Optional: switch Wi-Fi off once time is synced (less heat, less radio contention with BLE).
  if (g_settings.wifi_auto_off && connected && ntp_started && !pending && !held() &&
      now - connected_since > 10000UL) {
    struct tm t;
    if (wifi_get_time(&t)) {
      Serial.println("Time synced, Wi-Fi going to sleep");
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      asleep = true;
      asleep_since = now;
      ntp_started = false;
    }
  }
}

const char* wifi_status_text(void) {
  static char buf[64];
  if (asleep) return "Wi-Fi sleeping";
  if (pending) {
    snprintf(buf, sizeof(buf), "Connecting: %s", pend_ssid);
    return buf;
  }
  if (WiFi.status() == WL_CONNECTED) {
    snprintf(buf, sizeof(buf), "Connected: %s", WiFi.SSID().c_str());
    return buf;
  }
  if (failed) return "Connection failed";
  if (!saved_ssid[0]) return "No network set";
  return "Connecting...";
}
