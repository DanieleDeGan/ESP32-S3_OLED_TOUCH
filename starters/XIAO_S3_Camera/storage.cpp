#include "storage.h"

#include <SPI.h>
#include <Preferences.h>   // bundled nel core ESP32: contatori persistenti in NVS

// Chip select della microSD sulla scheda Sense (vedi nota in storage.h:
// e' anche il LED utente della XIAO). ATTENZIONE: lo schematico Seeed
// riporta GPIO3, ma e' un errore noto — il pin buono, confermato anche dal
// loro tutorial e dagli utenti, e' il 21.
#define SD_CS_PIN 21

// Frequenza del bus SPI verso la card: 4 MHz e' il valore degli esempi
// ufficiali Seeed, prudente e sempre funzionante. Si puo' alzare (10-20 MHz)
// per scritture piu' rapide, a rischio di mount falliti con card lente.
// I pin SCK/MISO/MOSI non si passano: SD.begin() chiama SPI.begin() e i
// default della variante XIAO_ESP32S3 sono gia' 7/8/9 (D8/D9/D10).
#define SD_SPI_HZ 4000000

#define LOG_HEADER "boot_id,n,secondi_da_accensione,sorgente,file,byte,notifica_hub"

#define NVS_KEY_BOOTID "boot_id"
#define NVS_KEY_IMGIDX "img_idx"

static bool     s_mounted    = false;
static char     s_lastError[48] = "non ancora montata";
static uint32_t s_bootId     = 0;
static uint32_t s_imgIndex   = 0;   // progressivo del PROSSIMO scatto
static uint32_t s_photoCount = 0;
static uint32_t s_eventN     = 0;   // n. progressivo di riga nel CSV, per questa accensione

// ---------------------------------------------------------------------
//  NVS: contatori che devono sopravvivere al riavvio
// ---------------------------------------------------------------------
static uint32_t nvs_bump(const char* key, uint32_t step) {
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, /*readOnly=*/false)) return 0;
  uint32_t v = prefs.getUInt(key, 0) + step;
  prefs.putUInt(key, v);
  prefs.end();
  return v;
}

static void nvs_set(const char* key, uint32_t value) {
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, /*readOnly=*/false)) return;
  prefs.putUInt(key, value);
  prefs.end();
}

static uint32_t nvs_get(const char* key) {
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, /*readOnly=*/true)) return 0;
  uint32_t v = prefs.getUInt(key, 0);
  prefs.end();
  return v;
}

uint32_t sd_boot_id() {
  if (s_bootId == 0) s_bootId = nvs_bump(NVS_KEY_BOOTID, 1);
  return s_bootId;
}

// ---------------------------------------------------------------------
//  Mount
// ---------------------------------------------------------------------
static void countPhotos() {
  s_photoCount = 0;
  File dir = SD.open(SD_PHOTO_DIR);
  if (!dir) return;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    if (!f.isDirectory()) s_photoCount++;
    f.close();
  }
  dir.close();
  if (s_photoCount > 0) s_photoCount--;   // eventi.csv non e' una foto
}

bool sd_begin() {
  sd_boot_id();   // fissa il boot_id anche se la card non c'e'

  if (s_mounted) return true;

  if (!SD.begin(SD_CS_PIN, SPI, SD_SPI_HZ)) {
    snprintf(s_lastError, sizeof(s_lastError), "mount fallito (card assente?)");
    return false;
  }
  if (SD.cardType() == CARD_NONE) {
    SD.end();
    snprintf(s_lastError, sizeof(s_lastError), "nessuna card nello slot");
    return false;
  }

  if (!SD.exists(SD_PHOTO_DIR) && !SD.mkdir(SD_PHOTO_DIR)) {
    SD.end();
    snprintf(s_lastError, sizeof(s_lastError), "impossibile creare %s (FAT32?)", SD_PHOTO_DIR);
    return false;
  }

  // Intestazione del CSV solo se il file non c'e' ancora: cosi' i log di
  // accensioni diverse si accodano nello stesso file senza ripeterla.
  if (!SD.exists(SD_LOG_PATH)) {
    File f = SD.open(SD_LOG_PATH, FILE_WRITE);
    if (f) {
      f.println(LOG_HEADER);
      f.close();
    }
  }

  s_mounted  = true;
  s_imgIndex = nvs_get(NVS_KEY_IMGIDX);
  countPhotos();
  snprintf(s_lastError, sizeof(s_lastError), "ok");
  Serial.printf("[SD] montata: %llu MB totali, %llu MB usati, %lu foto in %s\n",
                sd_total_mb(), sd_used_mb(), (unsigned long)s_photoCount, SD_PHOTO_DIR);
  return true;
}

bool        sd_mounted()     { return s_mounted; }
const char* sd_last_error()  { return s_lastError; }
uint32_t    sd_photo_count() { return s_photoCount; }
uint64_t    sd_total_mb()    { return s_mounted ? SD.cardSize() / (1024ULL * 1024ULL) : 0; }
uint64_t    sd_used_mb()     { return s_mounted ? SD.usedBytes() / (1024ULL * 1024ULL) : 0; }

// ---------------------------------------------------------------------
//  Scrittura
// ---------------------------------------------------------------------
bool sd_save_photo(const uint8_t* data, size_t len, char* name_out, size_t name_cap) {
  if (name_out && name_cap) name_out[0] = '\0';
  if (!s_mounted || data == nullptr || len == 0) return false;

  // Il progressivo vive in NVS, ma la card puo' essere stata sostituita con
  // una che ha gia' quei nomi: in quel caso si va avanti finche' il nome e'
  // libero, invece di sovrascrivere foto altrui.
  char path[48];
  for (int tries = 0; tries < 1000; tries++) {
    snprintf(path, sizeof(path), SD_PHOTO_DIR "/IMG_%05lu.JPG", (unsigned long)s_imgIndex);
    if (!SD.exists(path)) break;
    s_imgIndex++;
  }

  File f = SD.open(path, FILE_WRITE);
  if (!f) {
    snprintf(s_lastError, sizeof(s_lastError), "apertura %s fallita", path);
    return false;
  }
  size_t written = f.write(data, len);
  f.close();

  if (written != len) {
    SD.remove(path);   // meglio nessun file che un JPEG troncato
    snprintf(s_lastError, sizeof(s_lastError), "scritti %u/%u byte (card piena?)",
             (unsigned)written, (unsigned)len);
    return false;
  }

  s_imgIndex++;
  nvs_set(NVS_KEY_IMGIDX, s_imgIndex);
  s_photoCount++;
  if (name_out && name_cap) {
    snprintf(name_out, name_cap, "%s", path + strlen(SD_PHOTO_DIR) + 1);
  }
  return true;
}

void sd_log_event(const char* sorgente, const char* file, size_t bytes, bool notificato_hub) {
  s_eventN++;
  if (!s_mounted) return;
  File f = SD.open(SD_LOG_PATH, FILE_APPEND);
  if (!f) return;
  f.printf("%lu,%lu,%lu,%s,%s,%u,%d\n",
           (unsigned long)s_bootId,
           (unsigned long)s_eventN,
           (unsigned long)(millis() / 1000),
           sorgente ? sorgente : "?",
           (file && file[0]) ? file : "-",
           (unsigned)bytes,
           notificato_hub ? 1 : 0);
  f.close();
}

// ---------------------------------------------------------------------
//  Lettura / gestione file (usata dalla web UI)
// ---------------------------------------------------------------------
int sd_list_photos(sd_photo_cb_t cb, void* arg, int max_items) {
  if (!s_mounted || cb == nullptr) return 0;
  File dir = SD.open(SD_PHOTO_DIR);
  if (!dir) return 0;

  int n = 0;
  for (File f = dir.openNextFile(); f && n < max_items; f = dir.openNextFile()) {
    if (!f.isDirectory()) {
      const char* nm = f.name();
      // f.name() puo' tornare il percorso completo a seconda della versione
      // del core: tieni solo la parte dopo l'ultimo '/'.
      const char* slash = strrchr(nm, '/');
      if (slash) nm = slash + 1;
      if (strcmp(nm, "eventi.csv") != 0) {
        cb(nm, f.size(), arg);
        n++;
      }
    }
    f.close();
  }
  dir.close();
  return n;
}

bool sd_name_is_safe(const char* name) {
  if (name == nullptr || name[0] == '\0' || strlen(name) > 32) return false;
  if (strstr(name, "..") || strchr(name, '/') || strchr(name, '\\')) return false;
  return true;
}

File sd_open_photo(const char* name) {
  if (!s_mounted || !sd_name_is_safe(name)) return File();
  char path[48];
  snprintf(path, sizeof(path), SD_PHOTO_DIR "/%s", name);
  return SD.open(path, FILE_READ);
}

bool sd_delete_photo(const char* name) {
  if (!s_mounted || !sd_name_is_safe(name)) return false;
  char path[48];
  snprintf(path, sizeof(path), SD_PHOTO_DIR "/%s", name);
  if (!SD.remove(path)) return false;
  if (s_photoCount > 0) s_photoCount--;
  return true;
}
