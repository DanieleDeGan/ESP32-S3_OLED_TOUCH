/**
 * WSOLED_SD.cpp
 *
 * Sottile strato sopra la libreria SD_MMC del core Arduino ESP32 (non su
 * esp_vfs_fat_* grezzo): fissa il wiring della scheda, distingue i motivi di
 * fallimento e offre l'unica primitiva che serve a un logger, l'append di una
 * riga. Vedi WSOLED_SD.h per il perche' di SDMMC 1 bit e per la nota V1/V2.
 */

#include "WSOLED_SD.h"

#include <Arduino.h>
#include <SD_MMC.h>

// Wiring V2 (vedi header). Non sono configurabili di proposito: sono saldati
// sulla scheda, non una scelta di progetto.
#define SD_PIN_CLK  9
#define SD_PIN_CMD  42
#define SD_PIN_D0   8

static bool        s_mounted    = false;
static const char *s_last_error = "";

// ---------------------------------------------------------------------------
bool SDCard_Init(void)
{
    if (s_mounted) return true;

    if (!SD_MMC.setPins(SD_PIN_CLK, SD_PIN_CMD, SD_PIN_D0)) {
        s_last_error = "setPins fallita";
        return false;
    }

    // mode1bit = true, format_if_mount_failed = false (vedi header),
    // 20 MHz invece del massimo: in 1 bit la frequenza alta e' la prima causa
    // di card che "a volte" non montano.
    if (!SD_MMC.begin("/sdcard", true, false, SDMMC_FREQ_DEFAULT)) {
        s_last_error = "mount fallito: card assente, non FAT32, o scheda V1";
        return false;
    }

    if (SD_MMC.cardType() == CARD_NONE) {
        SD_MMC.end();
        s_last_error = "nessuna card nello slot";
        return false;
    }

    s_mounted    = true;
    s_last_error = "";
    return true;
}

// ---------------------------------------------------------------------------
bool SDCard_IsMounted(void)     { return s_mounted; }
const char *SDCard_LastError(void) { return s_last_error; }

uint32_t SDCard_SizeMB(void)
{
    if (!s_mounted) return 0;
    return (uint32_t)(SD_MMC.cardSize() / (1024ULL * 1024ULL));
}

// ---------------------------------------------------------------------------
bool SDCard_Exists(const char *path)
{
    return s_mounted && SD_MMC.exists(path);
}

// ---------------------------------------------------------------------------
bool SDCard_AppendLine(const char *path, const char *line)
{
    if (!s_mounted) { s_last_error = "SD non montata"; return false; }

    File f = SD_MMC.open(path, FILE_APPEND);
    if (!f) { s_last_error = "apertura file fallita"; return false; }

    size_t written = f.println(line);
    f.close();

    if (written == 0) {
        s_last_error = "scrittura fallita (card piena o protetta?)";
        return false;
    }
    s_last_error = "";
    return true;
}

// ---------------------------------------------------------------------------
bool SDCard_WriteHeaderIfNew(const char *path, const char *header)
{
    if (!s_mounted) { s_last_error = "SD non montata"; return false; }
    if (SD_MMC.exists(path)) return true;
    return SDCard_AppendLine(path, header);
}
