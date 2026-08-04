#pragma once
#include <Arduino.h>
#include <FS.h>
#include <SD.h>

// =====================================================================
//  storage — microSD della scheda di espansione Sense
//
//  La microSD della XIAO Sense sta su SPI, non su SDMMC come quella della
//  board AMOLED (percio' questo file NON usa la libreria AMOLED191_SD, che e'
//  scritta per l'altra scheda):
//      CS = GPIO21, SCK = GPIO7 (D8), MISO = GPIO8 (D9), MOSI = GPIO9 (D10)
//
//  ATTENZIONE: GPIO21 e' anche il LED utente della XIAO. Con la Sense
//  montata quel pin e' il chip select della SD, quindi il LED lampeggia da
//  solo ad ogni accesso alla card e non e' utilizzabile come spia di stato.
//  E lo slot occupa l'intero bus SPI: finche' usi la microSD, D8/D9/D10 non
//  sono disponibili per altre periferiche SPI (sulla scheda Sense c'e' il
//  ponticello J3 per scollegare la card e riprendersi il bus).
//
//  Come in AMOLED191_SD, ogni scrittura apre / scrive / chiude: piu' lento, ma
//  un'interruzione di corrente (o l'estrazione della card) non lascia dati
//  in sospeso. Card <= 64 GB formattate FAT32; le exFAT non montano.
// =====================================================================

#define SD_PHOTO_DIR "/foto"
#define SD_LOG_PATH  "/foto/eventi.csv"

// Namespace NVS condiviso con lo sketch (contatori qui, impostazioni li').
#define NVS_NAMESPACE "xiaocam"

// Monta la card. false = card assente o non FAT32: lo sketch deve
// continuare a funzionare lo stesso (web UI e notifiche restano attive,
// solo senza salvataggio). Richiamabile a runtime dopo aver inserito
// una card.
bool sd_begin();
bool sd_mounted();

// Ultimo errore in chiaro, per la web UI ("card assente", "FAT32?"...).
const char* sd_last_error();

// Contatore di accensioni tenuto in NVS: e' la prima colonna del CSV, e
// serve a distinguere due run diverse dentro lo stesso file (la scheda non
// ha un orologio, quindi "secondi da accensione" da solo e' ambiguo).
uint32_t sd_boot_id();

uint64_t sd_total_mb();
uint64_t sd_used_mb();
uint32_t sd_photo_count();

// Salva un JPEG in SD_PHOTO_DIR come IMG_<progressivo>.JPG. Il progressivo
// vive in NVS, quindi non riparte da zero ad ogni riavvio. name_out riceve
// il solo nome del file (non il percorso). false = card non montata o
// scrittura fallita.
bool sd_save_photo(const uint8_t* data, size_t len, char* name_out, size_t name_cap);

// Aggiunge una riga al CSV degli eventi. sorgente = "PIR" | "WEB" | "HUB".
// file puo' essere "" se lo scatto non e' stato salvato.
void sd_log_event(const char* sorgente, const char* file, size_t bytes, bool notificato_hub);

// Elenco delle foto (in ordine di directory: i nomi sono progressivi con
// zero iniziali, quindi ordinarli alfabeticamente equivale a ordinarli per
// data). Ritorna quante ne ha passate alla callback.
typedef void (*sd_photo_cb_t)(const char* name, size_t size, void* arg);
int sd_list_photos(sd_photo_cb_t cb, void* arg, int max_items);

// true se `name` e' un nome di file semplice e sicuro (niente "/" ne'
// ".."): da usare SEMPRE sui nomi che arrivano da una richiesta web, o si
// apre la card intera a chi conosce l'URL.
bool sd_name_is_safe(const char* name);

// Apre una foto per la lettura (web UI). Il chiamante deve chiuderla.
File sd_open_photo(const char* name);
bool sd_delete_photo(const char* name);
