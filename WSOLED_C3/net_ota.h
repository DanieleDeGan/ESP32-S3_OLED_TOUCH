#pragma once
#include <Arduino.h>

// =====================================================================
//  net_ota — WiFi + OTA (boilerplate isolato)
//
//  Porta su:
//   1) la connessione WiFi (station),
//   2) ArduinoOTA  -> upload da Arduino IDE (Tools > Port > porta di rete),
//   3) un web server con pagina  /update  -> upload del .bin da browser.
//
//  Nel .ino resta solo la logica applicativa + il disegno a schermo.
// =====================================================================

// Callback opzionale invocata durante un aggiornamento OTA, per mostrare
// a schermo cosa sta succedendo. percent = 0..100, oppure -1 se sconosciuto
// (upload web a dimensione ignota). Impostala PRIMA di net_begin().
typedef void (*ota_progress_cb_t)(int percent, const char* what);
void net_setOtaProgressCb(ota_progress_cb_t cb);

// Da chiamare una volta in setup() (dopo Serial e, se vuoi il feedback a
// schermo durante l'update, dopo aver inizializzato il display).
void net_begin();

// Da chiamare a ogni giro di loop(): gestisce ArduinoOTA + richieste web.
void net_loop();

// Stato utile alla UI.
bool   net_isConnected();
String net_ip();
