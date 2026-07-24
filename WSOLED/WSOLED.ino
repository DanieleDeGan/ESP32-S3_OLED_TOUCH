/**
 * WSOLED.ino  —  starter LVGL + SquareLine Studio (sketch vuoto)
 *
 * Tutto il boilerplate display/touch/LVGL e' in lvgl_port.cpp/.h.
 * Qui resta SOLO la tua logica: UI, WiFi, SD, sensori.
 *
 * Per un esempio funzionante vedi examples/Orientation_IMU.
 *
 * REGOLA FONDAMENTALE:
 *   Il rendering LVGL gira in un task dedicato. Ogni volta che leggi o
 *   modifichi un oggetto LVGL da QUI (loop, task tuoi, callback WiFi),
 *   devi avvolgere il codice in lvgl_lock() / lvgl_unlock().
 *
 * ARDUINO IDE (Tools):
 *   Board=ESP32S3 Dev Module | Flash 16MB
 *   Partizione="16M Flash (3MB APP/9.9MB FATFS)"
 *   PSRAM=OPI PSRAM | USB CDC On Boot=Enabled
 */

#include "lvgl_port.h"
#include "ui.h"

// #include <WiFi.h>
// #include <SD.h>

static uint32_t last_update = 0;

static float read_sensor(void)
{
    // TODO: leggi qui il tuo sensore reale.
    return 23.5f;
}

void setup()
{
    Serial.begin(115200);

    // 1) Display + touch + LVGL + task di rendering
    lvgl_port_init();

    // 2) UI generata da SquareLine: si creano oggetti LVGL -> serve il lock
    if (lvgl_lock(-1)) {
        ui_init();
        lvgl_unlock();
    }

    // 3) La tua periferica: WiFi, SD, sensori...
    //    SD: ATTENZIONE, su questa scheda la microSD CONDIVIDE i pin col
    //    display (CLK=GPIO47, MISO=GPIO8); MOSI=GPIO42, CS=GPIO9. Non e' un
    //    bus indipendente: segui il demo SD_Test di Waveshare.
}

void loop()
{
    if (millis() - last_update >= 1000) {
        last_update = millis();

        float t = read_sensor();
        char buf[16];
        snprintf(buf, sizeof(buf), "%.1f C", t);

        if (lvgl_lock(-1)) {
            // lv_label_set_text(ui_LabelTemp, buf);   // <- tuo oggetto SquareLine
            lvgl_unlock();
        }
    }

    delay(5);
}
