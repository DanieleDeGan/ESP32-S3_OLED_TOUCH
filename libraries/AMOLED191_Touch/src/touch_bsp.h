#ifndef TOUCH_BSP_H
#define TOUCH_BSP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Inizializza il bus I2C condiviso (via Core_I2CBusInit()) e il touch FT3168. */
void Touch_Init(void);

/** Legge il punto di tocco corrente. Ritorna 1 se c'e' un tocco, 0 altrimenti. */
uint8_t getTouch(uint16_t *x, uint16_t *y);

#ifdef __cplusplus
}
#endif

#endif
