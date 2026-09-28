// Example TFT_eSPI setup for the ESP32-2424S012 (GC9A01, 240x240, ESP32-C3).
// Copy the relevant lines into libraries/TFT_eSPI/User_Setup.h.
// !! Verify every pin against your board's documentation / working sketch.
// !! Do NOT define TFT_BL here - the backlight is driven by backlight.cpp (PWM).

#define GC9A01_DRIVER
#define TFT_WIDTH  240
#define TFT_HEIGHT 240

#define TFT_MISO -1
#define TFT_MOSI 7
#define TFT_SCLK 6
#define TFT_CS   10
#define TFT_DC   2
#define TFT_RST  -1

#define SPI_FREQUENCY 40000000
#define LOAD_GLCD
