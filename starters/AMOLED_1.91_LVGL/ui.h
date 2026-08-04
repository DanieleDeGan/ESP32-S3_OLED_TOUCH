/**
 * ui.h  —  STUB segnaposto
 *
 * Fa compilare il progetto PRIMA di avere l'export di SquareLine Studio.
 * Espone lo stesso contratto che genera SquareLine: void ui_init(void).
 * Quando esporti la tua UI da SquareLine, i suoi ui.h/ui.c sostituiscono
 * questi due file e il resto del progetto non cambia.
 */

#ifndef UI_H
#define UI_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void ui_init(void);

#ifdef __cplusplus
}
#endif

#endif // UI_H
