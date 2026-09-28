/**
 * Minimal lv_conf.h for LVGL 8.3.x - ESP32-C3 Round RuuviTag Display Hub.
 *
 * Install: copy this file to  <Arduino>/libraries/lv_conf.h
 *          (i.e. NEXT TO the "lvgl" folder, not inside it).
 *
 * Every option that is not listed here falls back to LVGL's own default
 * (lv_conf_internal.h), which enables all widgets used by this project.
 * For the complete list of options see lvgl/lv_conf_template.h.
 */

#if 1   /* "1" = enable this file (the template ships with "0") */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* Colour: RGB565, no byte swap (the sketch swaps while pushing to TFT_eSPI) */
#define LV_COLOR_DEPTH     16
#define LV_COLOR_16_SWAP   0

/* Memory used by LVGL widgets */
#define LV_MEM_CUSTOM      0
#define LV_MEM_SIZE        (40U * 1024U)

/* Time base: the sketch calls lv_tick_inc() itself (only when this is 0) */
#define LV_TICK_CUSTOM     0

/* Fonts used by the UI */
#define LV_FONT_MONTSERRAT_14   1     /* default font, footer/labels, symbols */
#define LV_FONT_MONTSERRAT_16   1     /* clock, arrows */
#define LV_FONT_MONTSERRAT_48   1     /* temperature */
#define LV_FONT_DEFAULT         &lv_font_montserrat_14

#endif /*LV_CONF_H*/

#endif /*End of "Content enable"*/
