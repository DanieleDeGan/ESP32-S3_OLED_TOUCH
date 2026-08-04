/**
 * AMOLED191_Touch.h
 *
 * Touch capacitivo FT3168 (I2C) della Waveshare ESP32-S3-Touch-AMOLED-1.91.
 *
 * Uso tipico (dopo AMOLED191_Display::Display_Init()):
 *   Touch_Init();               // I2C (via AMOLED191_Core) + setup FT3168
 *   Touch_RegisterLvglIndev();  // opzionale: registra il touch come input LVGL
 *
 * Touch_Init()/getTouch() non hanno alcuna dipendenza da LVGL. Solo
 * Touch_RegisterLvglIndev() richiede che Display_Init() sia gia' stato
 * chiamato (lv_init() gia' avvenuto).
 */

#ifndef AMOLED191_TOUCH_H
#define AMOLED191_TOUCH_H

#include "touch_bsp.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Registra il touch come LVGL input device (pointer). Chiamare dopo
 * Display_Init() e Touch_Init(). Sicura anche se il task di rendering LVGL
 * e' gia' partito (usa lvgl_lock()/lvgl_unlock() internamente).
 */
void Touch_RegisterLvglIndev(void);

#ifdef __cplusplus
}
#endif

#endif // AMOLED191_TOUCH_H
