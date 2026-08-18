#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

void init_wifi_manager(void);
void tick_wifi_manager(void);
void scan_and_populate_wifi(void);
void save_selected_ssid(const char* ssid);
void connect_to_new_wifi(const char* password);

#endif
