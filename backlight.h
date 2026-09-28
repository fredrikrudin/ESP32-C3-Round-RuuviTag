#pragma once
#include <stdint.h>
void backlight_init(uint8_t initial);
void backlight_set(uint8_t value);   // no-op if unchanged
