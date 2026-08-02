#include "settings.h"
#include <Preferences.h>

#define NVS_NS "envcfg"

#define DEFAULT_NODE_NAME "NodoAmbiente"
#define DEFAULT_LOG_INT_S 60
#define DEFAULT_PAGE_SEC  6
#define DEFAULT_TZ        "CET-1CEST,M3.5.0,M10.5.0/3"   // Europe/Rome, DST automatico

static AppSettings s_settings;

// ---------------------------------------------------------------------
//  Helper NVS: apri / leggi-o-scrivi / chiudi, mai una Preferences tenuta
//  aperta piu' del necessario (stesso pattern di storage.cpp).
// ---------------------------------------------------------------------
static bool nvs_put_string(const char* key, const char* value) {
  Preferences prefs;
  if (!prefs.begin(NVS_NS, /*readOnly=*/false)) return false;
  prefs.putString(key, value);
  prefs.end();
  return true;
}

static bool nvs_put_u32(const char* key, uint32_t value) {
  Preferences prefs;
  if (!prefs.begin(NVS_NS, /*readOnly=*/false)) return false;
  prefs.putUInt(key, value);
  prefs.end();
  return true;
}

static bool nvs_put_float4(const char* k1, float v1, const char* k2, float v2,
                            const char* k3, float v3, const char* k4, float v4) {
  Preferences prefs;
  if (!prefs.begin(NVS_NS, /*readOnly=*/false)) return false;
  prefs.putFloat(k1, v1);
  prefs.putFloat(k2, v2);
  prefs.putFloat(k3, v3);
  prefs.putFloat(k4, v4);
  prefs.end();
  return true;
}

// ---------------------------------------------------------------------
void settings_begin() {
  strlcpy(s_settings.nodeName, DEFAULT_NODE_NAME, sizeof(s_settings.nodeName));
  s_settings.logIntervalS = DEFAULT_LOG_INT_S;
  s_settings.pageSeconds  = DEFAULT_PAGE_SEC;
  s_settings.comfort      = COMFORT_DEFAULT;
  strlcpy(s_settings.tz, DEFAULT_TZ, sizeof(s_settings.tz));

  Preferences prefs;
  if (!prefs.begin(NVS_NS, /*readOnly=*/true)) {
    // NVS non disponibile (namespace mai creato, o errore flash): restano i
    // default appena impostati sopra. Non e' un errore fatale: l'app deve
    // funzionare comunque, solo senza persistenza delle modifiche.
    return;
  }

  String name = prefs.getString("node_name", DEFAULT_NODE_NAME);
  strlcpy(s_settings.nodeName, name.c_str(), sizeof(s_settings.nodeName));

  s_settings.logIntervalS = prefs.getUInt("log_int_s", DEFAULT_LOG_INT_S);
  s_settings.pageSeconds  = prefs.getUInt("page_sec", DEFAULT_PAGE_SEC);

  s_settings.comfort.tMin = prefs.getFloat("t_min", COMFORT_DEFAULT.tMin);
  s_settings.comfort.tMax = prefs.getFloat("t_max", COMFORT_DEFAULT.tMax);
  s_settings.comfort.hMin = prefs.getFloat("h_min", COMFORT_DEFAULT.hMin);
  s_settings.comfort.hMax = prefs.getFloat("h_max", COMFORT_DEFAULT.hMax);

  String tz = prefs.getString("tz", DEFAULT_TZ);
  strlcpy(s_settings.tz, tz.c_str(), sizeof(s_settings.tz));

  prefs.end();
}

const AppSettings& settings_get() { return s_settings; }

// ---------------------------------------------------------------------
bool settings_set_node_name(const char* name) {
  if (!name) return false;
  size_t len = strlen(name);
  if (len == 0 || len >= sizeof(s_settings.nodeName)) return false;
  if (!nvs_put_string("node_name", name)) return false;
  strlcpy(s_settings.nodeName, name, sizeof(s_settings.nodeName));
  return true;
}

bool settings_set_log_interval_s(uint32_t seconds) {
  if (seconds < 5 || seconds > 3600) return false;   // sotto i 5s si stressa DHT11/SD
  if (!nvs_put_u32("log_int_s", seconds)) return false;
  s_settings.logIntervalS = seconds;
  return true;
}

bool settings_set_page_seconds(uint32_t seconds) {
  if (seconds < 2 || seconds > 30) return false;
  if (!nvs_put_u32("page_sec", seconds)) return false;
  s_settings.pageSeconds = seconds;
  return true;
}

bool settings_set_comfort(const ComfortConfig& cfg) {
  if (!(cfg.tMin < cfg.tMax) || !(cfg.hMin < cfg.hMax)) return false;
  if (cfg.tMin < -20.0f || cfg.tMax > 60.0f) return false;    // range fisico plausibile
  if (cfg.hMin < 0.0f || cfg.hMax > 100.0f) return false;
  if (!nvs_put_float4("t_min", cfg.tMin, "t_max", cfg.tMax, "h_min", cfg.hMin, "h_max", cfg.hMax)) {
    return false;
  }
  s_settings.comfort = cfg;
  return true;
}

bool settings_set_tz(const char* tzPosix) {
  if (!tzPosix) return false;
  size_t len = strlen(tzPosix);
  if (len == 0 || len >= sizeof(s_settings.tz)) return false;
  if (!nvs_put_string("tz", tzPosix)) return false;
  strlcpy(s_settings.tz, tzPosix, sizeof(s_settings.tz));
  return true;
}
