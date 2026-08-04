#pragma once
#include <Arduino.h>

// =====================================================================
//  comfort — indice di comfort termico/umidita', a soglie configurabili
//
//  Il DHT11 ha una precisione bassa (+-2 C, +-5% RH): niente Heat Index o
//  PMV/PPD "veri", che richiederebbero una precisione che il sensore non
//  ha. Si usa invece un punteggio a soglie: una banda "ideale" configurabile
//  [tMin,tMax] x [hMin,hMax], con penalita' lineare oltre i bordi che
//  satura a un tetto (6 C oltre il bordo di temperatura, 30 punti di
//  umidita' oltre il bordo di umidita' = penalita' massima da quel fattore).
//  Gli span di saturazione sono deliberatamente larghi rispetto alla
//  tolleranza del sensore: un singolo campione rumoroso al limite della
//  tolleranza sposta il punteggio di ~16 punti al massimo, non abbastanza
//  da far saltare l'etichetta da un estremo all'altro vicino ai bordi
//  della banda.
//
//  Header-only e puro (nessuno stato, nessun side effect): comodo da
//  chiamare sia dal .ino (OLED) sia da web_ui.cpp (API/dashboard) senza
//  dipendenze incrociate. Chi vuole smoothing sui dati rumorosi (es. media
//  mobile delle ultime letture) lo fa PRIMA di chiamare comfort_eval(): non
//  e' compito di questo modulo.
// =====================================================================

struct ComfortConfig {
  float tMin, tMax;   // banda ideale di temperatura, gradi C
  float hMin, hMax;   // banda ideale di umidita', % RH
};

// Banda di default: ragionevole per un interno abitato; il tetto alto
// sull'umidita' (65%) e' pensato anche per limitare il rischio di condensa
// tipico di un camper. Configurabile a runtime via /api/config.
static constexpr ComfortConfig COMFORT_DEFAULT = { 19.0f, 26.0f, 35.0f, 65.0f };

enum ComfortLevel : uint8_t {
  COMFORT_OK       = 0,   // score >= 80
  COMFORT_MARGINAL = 1,   // score 60..79
  COMFORT_BAD       = 2,  // score < 60
};

struct ComfortResult {
  int16_t     score;    // 0..100
  ComfortLevel level;
  const char* label;    // stringa statica, non va liberata
  bool        inRange;  // dentro il rettangolo [tMin,tMax]x[hMin,hMax] (per lo scatter)
};

// Quanto oltre il bordo della banda serve per saturare la penalita' di quel
// fattore al massimo (50 punti su 100 totali).
static constexpr float COMFORT_T_SATURATE_SPAN = 6.0f;   // gradi C oltre il bordo
static constexpr float COMFORT_H_SATURATE_SPAN = 30.0f;  // punti % RH oltre il bordo

static inline float comfort_clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

// Distanza (>=0) di `v` fuori dall'intervallo [lo,hi]; 0 se v e' dentro.
static inline float comfort_distanceOutside(float v, float lo, float hi) {
  if (v < lo) return lo - v;
  if (v > hi) return v - hi;
  return 0.0f;
}

static inline ComfortResult comfort_eval(float tempC, float humPct, const ComfortConfig& cfg) {
  float distT = comfort_distanceOutside(tempC, cfg.tMin, cfg.tMax);
  float distH = comfort_distanceOutside(humPct, cfg.hMin, cfg.hMax);

  float penaltyT = comfort_clampf(distT / COMFORT_T_SATURATE_SPAN * 50.0f, 0.0f, 50.0f);
  float penaltyH = comfort_clampf(distH / COMFORT_H_SATURATE_SPAN * 50.0f, 0.0f, 50.0f);

  ComfortResult r;
  r.score   = (int16_t)comfort_clampf(roundf(100.0f - penaltyT - penaltyH), 0.0f, 100.0f);
  r.inRange = (tempC >= cfg.tMin && tempC <= cfg.tMax && humPct >= cfg.hMin && humPct <= cfg.hMax);

  if (r.score >= 80) {
    r.level = COMFORT_OK;
    r.label = "Confortevole";
  } else if (r.score >= 60) {
    r.level = COMFORT_MARGINAL;
    r.label = "Accettabile";
  } else {
    r.level = COMFORT_BAD;
    bool tempOff = penaltyT > 0.0f;
    bool humOff  = penaltyH > 0.0f;
    if (tempOff && humOff) {
      r.label = "Scomodo";                                   // entrambi i fattori fuori banda
    } else if (tempOff) {
      r.label = (tempC < cfg.tMin) ? "Troppo freddo" : "Troppo caldo";
    } else {
      r.label = (humPct < cfg.hMin) ? "Troppo secco" : "Troppo umido";
    }
  }
  return r;
}
