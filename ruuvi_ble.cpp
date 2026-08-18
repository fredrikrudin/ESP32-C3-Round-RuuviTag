#include "ruuvi_ble.h"
#include <NimBLEDevice.h>
#include <lvgl.h>

extern lv_obj_t *lbl_temp;
extern lv_obj_t *lbl_hum_text;
extern lv_obj_t *lbl_pres;
extern lv_obj_t *arc_hum;

volatile int scan_interval = 5;
static unsigned long last_scan_time = 0;
static bool is_scanning = false;

struct RuuviData {
    float temp; float hum; float pres; int batt; bool fresh;
};
static RuuviData live_data = {0.0, 0.0, 0.0, 0, false};

class RuuviCallbacks: public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* advertisedDevice) {
        std::string sd = advertisedDevice->getManufacturerData();
        
        // Match Ruuvi Manufacturer ID (0x0499) and Raw Format 5 (0x05)
        if (sd.length() >= 24 && sd[1] == 0x99 && sd[0] == 0x04 && sd[2] == 0x05) {
            int16_t tRaw = (sd[3] << 8) | sd[4];
            uint16_t hRaw = (sd[5] << 8) | sd[6];
            uint16_t pRaw = (sd[7] << 8) | sd[8];
            uint16_t bRaw = (sd[15] << 8) | sd[16];

            live_data.temp = tRaw * 0.005;
            live_data.hum = hRaw * 0.0025;
            live_data.pres = (pRaw + 50000) / 100.0;
            live_data.batt = map((bRaw >> 5) + 1600, 2200, 3000, 0, 100);
            
            if(live_data.batt > 100) live_data.batt = 100;
            if(live_data.batt < 0) live_data.batt = 0;
            live_data.fresh = true;
        }
    }
};

static void update_lvgl_fields_cb(lv_timer_t * t) {
    if (live_data.fresh) {
        char buf[32];
        sprintf(buf, "%.1f°", live_data.temp); lv_label_set_text(lbl_temp, buf);
        sprintf(buf, "%d%% RH", (int)live_data.hum); lv_label_set_text(lbl_hum_text, buf);
        lv_arc_set_value(arc_hum, (int)live_data.hum);
        
        sprintf(buf, LV_SYMBOL_CHARGE " %.0f hPa\n" LV_SYMBOL_BATTERY " %d%%", live_data.pres, live_data.batt);
        lv_label_set_text(lbl_pres, buf);
        live_data.fresh = false;
    }
}

void init_ruuvi_ble(void) {
    NimBLEDevice::init("");
    NimBLEScan* pScan = NimBLEDevice::getScan();
    pScan->setAdvertisedDeviceCallbacks(new RuuviCallbacks(), false);
    pScan->setActiveScan(false);
    pScan->setInterval(200);
    pScan->setWindow(150);
    
    // Create UI thread safe timer running at 500ms intervals
    lv_timer_create(update_lvgl_fields_cb, 500, NULL);
}

void tick_ruuvi_ble(void) {
    unsigned long now = millis();
    NimBLEScan* pScan = NimBLEDevice::getScan();

    if (scan_interval == 0) {
        if (!is_scanning) { pScan->start(0, nullptr, false); is_scanning = true; }
    } else {
        if (is_scanning) { pScan->stop(); is_scanning = false; }
        if (!is_scanning && (now - last_scan_time >= (scan_interval * 1000))) {
            pScan->start(2, nullptr, false); // Active scan for 2 seconds window
            last_scan_time = millis();
            pScan->clearResults();           // Avoid RAM allocations leak
        }
    }
}
