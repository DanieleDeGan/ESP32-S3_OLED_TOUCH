#pragma once
#include <Arduino.h>
#include <time.h>
#include <FS.h>

// =====================================================================
//  sd_logger — microSD su modulo SPI HW-125 (NON quella SDMMC della board
//  AMOLED: qui serve un mount SPI esplicito, pin dedicati, vedi sd_logger.cpp)
//
//  Log CSV con rotazione giornaliera in /logs/YYYY-MM-DD.csv. Come in
//  storage.cpp (WSOLED_XIAO): ogni scrittura apre/scrive/chiude, mai un
//  file tenuto aperto a lungo — un distacco di corrente in mezzo a una
//  scrittura perde al piu' quella riga, non l'intero file.
//
//  sd_record_count_total()/today() sono contatori in RAM aggiornati a ogni
//  scrittura riuscita: MAI una scansione della SD per rispondere a queste
//  due funzioni. Il totale e' salvato in NVS a intervalli (non a ogni
//  scrittura: risparmia cicli di erase della flash su un contatore che
//  altrimenti si riscrive ogni pochi secondi per anni — vedi il commento
//  su REC_TOTAL_FLUSH_EVERY in sd_logger.cpp) — un crash tra un flush e
//  l'altro puo' far perdere l'aggiornamento delle ultime unita' al solo
//  contatore mostrato in UI, i dati nel CSV non sono mai a rischio.
// =====================================================================

#define SD_LOG_DIR "/logs"

// Monta la card. false = card assente o non FAT32: il chiamante deve
// continuare a funzionare lo stesso (OLED/dashboard restano attivi, solo
// senza logging). Richiamabile a runtime per ritentare il mount.
bool sd_begin();
bool sd_mounted();
const char* sd_last_error();

uint64_t sd_total_mb();
uint64_t sd_free_mb();

// Contatore di avvii in NVS (namespace separato da settings.cpp): utile in
// diagnostica seriale, non finisce nel CSV (l'ordine cronologico e' gia'
// garantito dal timestamp reale, a differenza di DHT11_SD_Logger.ino che
// non aveva un RTC).
uint32_t sd_boot_id();

uint32_t sd_record_count_total();
uint32_t sd_record_count_today();

// Accoda una riga al CSV del giorno di `ts` (ruota automaticamente il file
// se e' cambiato il giorno rispetto all'ultima chiamata). timeSource deve
// essere "NTP" o "STIMA" (vedi rtc_time.h). false = SD non montata o
// scrittura fallita (vedi sd_last_error()).
bool sd_log_sample(time_t ts, const char* timeSource, float tempC, float humPct);

// Elenco delle date disponibili (nomi dei file /logs/*.csv, senza estensione),
// in ordine di directory (che per nomi "YYYY-MM-DD.csv" e' gia' ordine
// cronologico). Ritorna quante ne ha passate alla callback.
typedef void (*sd_date_cb_t)(const char* isoDate, size_t fileSizeBytes, void* arg);
int sd_list_days(sd_date_cb_t cb, void* arg, int maxItems);

// Legge il CSV di `isoDate` riga per riga, chiamando cb per ognuna (mai
// l'intero file in RAM: usata dall'API web per rispondere in streaming).
// Ritorna 0 se la data non esiste o non e' nel formato "YYYY-MM-DD".
typedef void (*sd_row_cb_t)(time_t ts, float tempC, float humPct, void* arg);
int sd_read_day(const char* isoDate, sd_row_cb_t cb, void* arg);

// true se `isoDate` e' esattamente nel formato "YYYY-MM-DD" (10 char,
// trattini nelle posizioni giuste, resto cifre) — da usare SEMPRE su una
// data che arriva da una query string web, prima di comporci un path.
bool sd_name_is_safe(const char* isoDate);

// Elimina il file /logs/<isoDate>.csv. Se isoDate e' il giorno corrente
// (quello su cui sd_log_sample() sta scrivendo), azzera anche il contatore
// "record di oggi" in RAM: altrimenti resterebbe a un valore che non
// corrisponde piu' a nessuna riga sul file (che verra' ricreato da zero,
// con una nuova intestazione, alla prossima scrittura). NON tocca il
// contatore cumulativo sd_record_count_total(), che conta le scritture
// fatte nella vita del dispositivo, non i file presenti in questo momento.
// false = data non valida, file inesistente, o cancellazione fallita.
bool sd_delete_day(const char* isoDate);

// Apre il file CSV grezzo di `isoDate` per la lettura (es. per un download
// dalla dashboard, servito con WebServer::streamFile() invece che riga per
// riga come sd_read_day()). Il chiamante deve chiudere il File ricevuto.
// Ritorna un File "falsy" (operator bool() == false) se la data non e'
// valida, la SD non e' montata, o il file non esiste.
File sd_open_day(const char* isoDate);
