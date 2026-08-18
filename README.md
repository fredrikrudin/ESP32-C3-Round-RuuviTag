# ESP32-C3 Round RuuviTag Display Hub (GC9A01) (By Gemini AI)

A modern, wearable/smartwatch-inspired dashboard interface built completely using **LVGL v8** for circular ESP32 boards (such as the ESP32-2424S012 / GC9A01 screen module). The hub dynamically intercepts BLE advertisements from nearby **RuuviTags**, parsing and rendering live real-time values including Temperature, Relative Humidity, Air Pressure, and sensor battery levels.

##Hardware: 
ESP32 Display 1.28 inch ESP32-C3 WiFi+BLE TFT LCD Module ESP32-2424S012 240x240 GC9A01 Touch Screen for Arduino IoT Smart Home

##AliExpress: 
https://www.aliexpress.com/item/1005007051709033.html

## Features
- 🔹 **Deep Blue Neon UI**: High-contrast, clean circular graphs and layouts.
- 🔹 **Real-time Synchronization Clock**: Internet NTP-time displayed in 24h format.
- 🔹 **On-Screen Wi-Fi Manager**: Interactively scan and connect to local Wi-Fi hotspots via touch keyboard inputs.
- 🔹 **Flash Retention**: Credentials and configurations persist across reboots via the native NVS `Preferences` library.
- 🔹 **Smart Auto-Dimming**: Enforces user-defined quiet night hours via software-controlled PWM backlighting loops.
- 🔹 **Manual Swipe Gestures**: Swipe Up or Down anywhere on the main screen to modify screen brightness steps.
- 🔹 **BLE Duty Cycle Tuning**: Modify update intervals inside settings to conserve energy and eliminate chip thermal issues.

## Dependencies & Environment (Arduino IDE)
Ensure the following libraries are installed via your Library Manager:
- `NimBLE-Arduino` (by h2zero)
- `lvgl` (Strictly within the **8.x.x** lifecycle, e.g., v8.3.11)
- `TFT_eSPI` (by Bodmer)

### Driver Setup (`User_Setup.h`)
Navigate to your computer's `libraries/TFT_eSPI/` installation path and swap or append these active configurations into `User_Setup.h`:

```cpp
#define GC9A01_DRIVER
#define TFT_MISO -1
#define TFT_MOSI 7
#define TFT_SCLK 6
#define TFT_CS   2
#define TFT_DC   3
#define TFT_RST  10
#define TFT_BL   22
#define SPI_FREQUENCY  40000000
#define LOAD_GLCD
```

### Compiler Configurations (Tools Menu)
Ensure your target board compile settings match these options before hitting Upload:
- **Board**: `ESP32C3 Dev Module`
- **Flash Size**: `4MB`
- **Partition Scheme**: `Minimal SPIFFS (Large APPS with OTA)`
