#include "backlight.h"
#include <Arduino.h>
#include "config.h"

// Works on arduino-esp32 core 2.x (ledcSetup/ledcAttachPin) and 3.x (ledcAttach).
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  #define BL_CORE3 1
#else
  #define BL_CORE3 0
  #define BL_LEDC_CH 0
#endif

static int last_value = -1;

void backlight_init(uint8_t initial) {
#if BL_CORE3
  ledcAttach(PIN_BACKLIGHT, 5000, 8);
#else
  ledcSetup(BL_LEDC_CH, 5000, 8);
  ledcAttachPin(PIN_BACKLIGHT, BL_LEDC_CH);
#endif
  last_value = -1;
  backlight_set(initial);
}

void backlight_set(uint8_t value) {
  if ((int)value == last_value) return;
  last_value = value;
#if BL_CORE3
  ledcWrite(PIN_BACKLIGHT, value);
#else
  ledcWrite(BL_LEDC_CH, value);
#endif
}
