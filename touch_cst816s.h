#pragma once
#include <stdint.h>
bool touch_init();                              // false if the chip does not answer on I2C
bool touch_read(uint16_t* x, uint16_t* y);      // true while a finger is down
