/**
 * AMOLED191_SD.h
 *
 * microSD onboard della Waveshare ESP32-S3-Touch-AMOLED-1.91, in modalita'
 * SDMMC a 1 bit (CLK=GPIO9, CMD=GPIO42, D0=GPIO8). Pensata per il logging:
 * poche API, tutte "append di una riga di testo".
 *
 * IMPORTANTE - REVISIONE DELLA SCHEDA. Esistono due wiring diversi:
 *   - V2 (schede attuali): SDMMC 1 bit, CLK=9, CMD=42, D0=8. Nessun pin in
 *     comune col bus QSPI del pannello -> SD e LVGL convivono senza problemi,
 *     ed e' quello che implementa questa libreria.
 *   - V1 (schede vecchie): SD su SPI3_HOST con CLK=47, cioe' LO STESSO pin del
 *     PCLK del display. Li' i due dispositivi sono fisicamente sullo stesso
 *     clock e non basta cambiare i #define: servirebbe condividere l'host SPI2
 *     fra pannello e card, riscrivendo anche AMOLED191_Display. NON supportata qui.
 * Se SDCard_Init() fallisce con una card sicuramente buona e formattata FAT32,
 * la scheda e' probabilmente una V1.
 *
 * Il wiring e i parametri qui sotto sono gli stessi dell'esempio ufficiale
 * Waveshare (repo waveshareteam/ESP32-S3-AMOLED-1.91, 02_Example/Arduino/
 * 04_SD_Card/sd_card_bsp.cpp): anche loro selezionano V1/V2 a compile-time con
 * un #ifdef VersionControl_V2, quindi non esiste un modo di distinguerle a
 * runtime - va guardata la scheda, o provata la V2 e vista se monta.
 *
 * NOTA HARDWARE: su V2 il GPIO9 porta anche il segnale TE (tearing effect) del
 * pannello. Il TE resta inattivo finche' non gli si manda il comando 0x35, che
 * la nostra sequenza di init non manda: non abilitarlo, o entra in conflitto
 * col clock della SD.
 *
 * FORMATO CARD: FAT32, capacita' fino a 64 GB. Le card exFAT non vengono
 * montate (e questa libreria NON le formatta da sola: cancellare la card di
 * qualcuno perche' "il mount non riesce" non e' un comportamento accettabile).
 *
 * NOTA THREADING: la SDMMC e' un controller a se', non condivide il bus col
 * rendering LVGL, quindi NON serve lvgl_lock() attorno a queste chiamate. Sono
 * pero' bloccanti (decine di ms per riga, di piu' se la card e' lenta):
 * chiamale da loop()/da un task tuo, mai da dentro una callback di evento LVGL.
 */

#ifndef AMOLED191_SD_H
#define AMOLED191_SD_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Monta la microSD. Idempotente e ri-tentabile: se e' gia' montata ritorna
 * subito true, se il mount fallisce lascia tutto pulito e si puo' richiamare
 * piu' tardi (utile per gestire la card inserita a board accesa).
 * In caso di fallimento il motivo e' leggibile con SDCard_LastError().
 */
bool SDCard_Init(void);

/** true se la card e' montata e utilizzabile. */
bool SDCard_IsMounted(void);

/**
 * Ultimo errore in forma testuale (italiano, breve, adatto a finire a schermo).
 * Stringa vuota se non ci sono stati errori.
 */
const char *SDCard_LastError(void);

/** Capacita' della card in MB (0 se non montata). */
uint32_t SDCard_SizeMB(void);

/** true se il file esiste gia' sulla card. */
bool SDCard_Exists(const char *path);

/**
 * Accoda una riga di testo al file (lo crea se non esiste) e aggiunge il
 * newline. Il file viene aperto e richiuso ad ogni riga: costa qualche
 * millisecondo in piu' ma se salta l'alimentazione - su un impianto a 12V
 * succede - si perde al massimo l'ultima riga, non l'intero file.
 */
bool SDCard_AppendLine(const char *path, const char *line);

/**
 * Scrive `header` come prima riga SOLO se il file non esiste ancora.
 * Serve a non ripetere l'intestazione del CSV ad ogni riavvio. Ritorna true
 * anche quando non c'era niente da fare (file gia' presente).
 */
bool SDCard_WriteHeaderIfNew(const char *path, const char *header);

#ifdef __cplusplus
}
#endif

#endif // AMOLED191_SD_H
