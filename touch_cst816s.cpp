#include "touch_cst816s.h"
#include <Arduino.h>
#include <Wire.h>
#include "config.h"

static const uint8_t CST816S_ADDR = 0x15;

bool touch_init() {
  pinMode(PIN_TOUCH_RST, OUTPUT);
  digitalWrite(PIN_TOUCH_RST, LOW);
  delay(10);
  digitalWrite(PIN_TOUCH_RST, HIGH);
  delay(60);

  Wire.begin(PIN_TOUCH_SDA, PIN_TOUCH_SCL);
  Wire.setClock(400000);

  Wire.beginTransmission(CST816S_ADDR);
  if (Wire.endTransmission() != 0) return false;

  // Disable the chip's auto-standby, otherwise it stops answering after a while.
  Wire.beginTransmission(CST816S_ADDR);
  Wire.write((uint8_t)0xFE);
  Wire.write((uint8_t)0x01);
  Wire.endTransmission();
  return true;
}

bool touch_read(uint16_t* x, uint16_t* y) {
  Wire.beginTransmission(CST816S_ADDR);
  Wire.write((uint8_t)0x01);                       // gesture, fingers, xh, xl, yh, yl
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)CST816S_ADDR, 6) != 6) return false;

  uint8_t b[6];
  for (int i = 0; i < 6; i++) b[i] = Wire.read();

  if ((b[1] & 0x0F) == 0) return false;            // no finger
  uint16_t tx = ((b[2] & 0x0F) << 8) | b[3];
  uint16_t ty = ((b[4] & 0x0F) << 8) | b[5];
  if (tx > 239) tx = 239;
  if (ty > 239) ty = 239;
  *x = tx;
  *y = ty;
  return true;
}
