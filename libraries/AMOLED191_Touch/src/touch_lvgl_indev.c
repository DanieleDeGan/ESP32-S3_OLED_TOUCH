/**
 * touch_lvgl_indev.c
 *
 * Wiring opzionale del touch FT3168 come LVGL input device (pointer). Separato
 * da touch_bsp.c cosi' che Touch_Init()/getTouch() restino utilizzabili anche
 * senza LVGL. Richiede che AMOLED191_Display::Display_Init() sia gia' stato
 * chiamato (serve lv_init() gia' fatto).
 */

#include "AMOLED191_Touch.h"
#include "AMOLED191_Display.h"
#include "lvgl.h"

static void lvgl_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    uint16_t x, y;
    if (getTouch(&x, &y)) {
        data->point.x = x;
        data->point.y = y;
        data->state   = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

void Touch_RegisterLvglIndev(void)
{
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = lvgl_touch_cb;

    // Display_Init() puo' aver gia' avviato il task di rendering: la
    // registrazione tocca le liste interne di LVGL che lv_timer_handler()
    // scorre in parallelo, quindi va protetta come qualunque altro accesso
    // LVGL fuori dal task di rendering.
    if (lvgl_lock(-1)) {
        lv_indev_drv_register(&indev_drv);
        lvgl_unlock();
    }
}
