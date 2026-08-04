/**
 * ui.c  —  STUB segnaposto (vedi ui.h)
 *
 * Crea una semplice label centrata, giusto per verificare che display, touch
 * e LVGL funzionino. Sostituiscilo con l'export di SquareLine quando pronto.
 */

#include "ui.h"

void ui_init(void)
{
    lv_obj_t *label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, "Starter pronto");
    lv_obj_center(label);
}
