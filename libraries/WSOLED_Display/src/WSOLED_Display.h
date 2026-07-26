/**
 * WSOLED_Display.h
 *
 * Modulo di porting LVGL per Waveshare ESP32-S3-AMOLED-1.91 (pannello SH8601,
 * QSPI). Incapsula bus QSPI, pannello, LVGL e il task di rendering. Non ha
 * alcuna dipendenza dal touch: per l'input aggiungi WSOLED_Touch e chiama
 * Touch_Init()/Touch_RegisterLvglIndev() DOPO Display_Init() (serve che
 * lv_init() sia gia' stato chiamato).
 *
 * Lo sketch deve solo chiamare Display_Init() e usare lvgl_lock()/lvgl_unlock().
 */

#ifndef WSOLED_DISPLAY_H
#define WSOLED_DISPLAY_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Inizializza SPI/QSPI, pannello SH8601, LVGL (buffer, driver, tick) e avvia
 * il task FreeRTOS che chiama lv_timer_handler(). Da chiamare UNA sola volta
 * in setup(), prima di Touch_Init()/Touch_RegisterLvglIndev() se presenti.
 */
void Display_Init(void);

/**
 * Prende il lock LVGL. OBBLIGATORIO prima di leggere/modificare qualunque
 * oggetto LVGL da un contesto diverso dal task LVGL: loop(), task sensori,
 * callback WiFi, ecc.
 *   timeout_ms = -1  -> attende all'infinito
 * Ritorna true se il lock e' stato acquisito.
 */
bool lvgl_lock(int timeout_ms);

/** Rilascia il lock preso con lvgl_lock(). */
void lvgl_unlock(void);

/**
 * Invia un comando al pannello via QSPI. Il framing QSPI (opcode 0x02,
 * comando nei bit 15:8) e' gestito internamente: passa il codice "nudo"
 * del registro (es. 0x51 per la luminosita').
 *   data = NULL e len = 0  -> comandi senza parametri (es. 0x29 Display On)
 *
 * NOTA THREAD-SAFETY: usa lo stesso bus QSPI del rendering LVGL. Se la chiami
 * a runtime dal loop()/da un task, avvolgila in lvgl_lock(-1)/lvgl_unlock().
 * Dentro l'init o dentro una callback di evento LVGL non serve (il task non
 * gira ancora, oppure il lock e' gia' preso).
 */
void lcd_command(uint8_t cmd, const uint8_t *data, size_t len);

/** Comodita': imposta la luminosita' (registro 0x51), 0 = min .. 255 = max. */
void lcd_set_brightness(uint8_t level);

/**
 * Legge un registro del pannello via QSPI (opcode 0x03). Riempie `data` con
 * `len` byte. Ritorna true se la transazione e' andata a buon fine.
 * Uso tipico: diagnostica (RDDID 0x04, RDDISBV 0x52, RDDMADCTR 0x0B, ...).
 * Vedi nota nella guida: la lettura QSPI e' meno affidabile della scrittura
 * e su alcune board puo' non restituire dati validi. Stesse regole di lock.
 */
bool lcd_read_register(uint8_t cmd, uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif // WSOLED_DISPLAY_H
