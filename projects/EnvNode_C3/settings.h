#pragma once
#include <Arduino.h>
#include "comfort.h"

// =====================================================================
//  settings — parametri utente persistiti in NVS (Preferences), stesso
//  pattern gia' usato per i contatori boot_id in storage.cpp/DHT11_SD_Logger
//  (apri/leggi-scrivi/chiudi, mai la NVS tenuta aperta).
//
//  Un solo "hub" di configurazione: .ino, sd_logger e web_ui leggono tutti
//  da settings_get(); solo web_ui (dalla dashboard) e il .ino (se servisse
//  un default diverso al primo avvio) chiamano i setter.
//
//  Nota su tz: settings_set_tz() persiste e aggiorna la copia in RAM, ma
//  NON riapplica da sola il fuso al clock di sistema (settings.cpp non
//  dipende da rtc_time.cpp) — chi la chiama (web_ui, dopo un POST
//  /api/config con tz cambiata) deve richiamare anche rtctime_begin(tz)
//  per rendere effettivo il cambio subito, non solo al prossimo riavvio.
// =====================================================================

struct AppSettings {
  char          nodeName[24];   // mostrato in dashboard/OLED
  uint32_t      logIntervalS;   // 5..3600 s: cadenza campionamento DHT + riga CSV
  uint32_t      pageSeconds;    // 2..30 s: rotazione automatica pagine OLED
  ComfortConfig comfort;
  char          tz[48];         // stringa POSIX TZ, es. "CET-1CEST,M3.5.0,M10.5.0/3"
};

// Carica da NVS (o applica i default se la NVS e' vuota/non disponibile).
// Da chiamare una volta in setup(), prima di tutto il resto.
void settings_begin();

const AppSettings& settings_get();

// Ogni setter valida l'input, persiste su NVS e aggiorna la copia in RAM.
// Ritorna false (senza toccare nulla) su input fuori range.
bool settings_set_node_name(const char* name);
bool settings_set_log_interval_s(uint32_t seconds);
bool settings_set_page_seconds(uint32_t seconds);
bool settings_set_comfort(const ComfortConfig& cfg);
bool settings_set_tz(const char* tzPosix);
