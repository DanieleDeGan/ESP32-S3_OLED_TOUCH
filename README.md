# Starter LVGL + SquareLine — Waveshare ESP32-S3-Touch-AMOLED-1.91

Template riutilizzabile per sviluppare interfacce LVGL (disegnate in SquareLine
Studio) sulla Waveshare ESP32-S3-Touch-AMOLED-1.91 (AMOLED 536×240, SH8601, touch
FT3168). Tutto il boilerplate di basso livello è isolato: nel tuo sketch resta
solo la tua logica.

## Contenuto della cartella

| File | Ruolo |
|------|-------|
| `WSOLED.ino` | sketch principale: `setup`/`loop`, la tua logica (UI, WiFi, SD, sensori) |
| `lvgl_port.h` / `.cpp` | display + touch + LVGL + task di rendering + mutex (non si tocca) |
| `esp_lcd_sh8601.h` / `.c` | driver del pannello SH8601 (Espressif/Waveshare) |
| `touch_bsp.h` / `.c` | driver touch FT3168 su I2C (Waveshare) |
| `lv_conf.h` | configurazione LVGL **a livello di progetto** |
| `build_opt.h` | flag di compilazione (vedi sotto) |
| `ui.h` / `ui.c` | **stub** segnaposto: vengono sostituiti dall'export di SquareLine |

Unica dipendenza esterna da installare: la libreria **LVGL 8.3.x** (il pannello e il
touch sono già nella cartella, non servono come librerie).

## Impostazioni Arduino IDE (Tools)

- Board: **ESP32S3 Dev Module**
- Flash Size: **16MB**
- Partition Scheme: **16M Flash (3MB APP/9.9MB FATFS)**
- PSRAM: **OPI PSRAM**
- USB CDC On Boot: **Enabled**
- CPU Frequency: **240 MHz**

## build_opt.h

Contiene due define passati globalmente al compilatore:

```
-DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE
```

- `LV_CONF_INCLUDE_SIMPLE` → LVGL cerca `lv_conf.h` lungo il percorso di include
  (quindi quello di progetto, in questa cartella) invece del percorso relativo fisso.
- `LV_LVGL_H_INCLUDE_SIMPLE` → i file generati da SquareLine includono `lvgl.h` in
  modo "semplice"; senza questo, dopo l'export si ottengono errori `lvgl.h: No such file`.

## Avviare un nuovo progetto

1. **Copia** l'intera cartella in `MioProgetto/`.
2. **Rinomina** lo sketch in `MioProgetto.ino` (il nome del `.ino` DEVE coincidere
   con quello della cartella, è un vincolo di Arduino).
3. **Compila e carica** così com'è: all'avvio compare "Starter pronto" centrato.
   Conferma che display, touch e LVGL funzionano (la UI è ancora lo stub).
4. In **SquareLine Studio** crea un nuovo progetto:
   - Risoluzione **536 × 240**, profondità colore **16 bit**.
   - Versione LVGL **8.3.x** (deve combaciare con `lv_conf.h` e la libreria installata).
5. **Esporta solo i file UI** ("Export UI Files") con percorso di export = la cartella
   dello sketch. I file `ui.h` / `ui.c` generati **sovrascrivono gli stub**; arrivano
   anche `ui_helpers.*`, `ui_events.*`, gli screen e gli asset.
   - **Non** usare il `.ino` né il driver TFT_eSPI generati da SquareLine: il display
     è già gestito da `lvgl_port`. Si tengono solo i file `ui_*`.
6. **Ricompila.** Se l'IDE non vede i file appena aggiunti, chiudilo e riaprilo (la
   cache di build conserva lo stato precedente).

## Dove scrivere la tua logica

- **Eventi dei widget**: per ogni evento "Call function" definito in SquareLine, scrivi
  il corpo in `ui_events.c`. Questo file **non viene sovrascritto** ai re-export.
- **Aggiornamenti UI dalla tua logica** (`loop`, task sensori, callback WiFi): la UI gira
  in un task dedicato, quindi avvolgi SEMPRE gli accessi a oggetti LVGL in
  `lvgl_lock(-1)` … `lvgl_unlock()`.
- **Dentro un callback di evento LVGL**: NON prendere il lock (ce l'hai già) e tieni il
  callback corto; il lavoro lento (SD, rete) va deferito al `loop()`/a un task.

## Note hardware

- **microSD**: condivide i pin col display (CLK=GPIO47, MISO=GPIO8; MOSI=GPIO42, CS=GPIO9).
  Non è un bus indipendente: segui il demo `SD_Test` di Waveshare. Schede ≤ 64 GB, FAT32.
- **I2C**: touch e IMU QMI8658 sono sullo stesso bus (SDA=GPIO40, SCL=GPIO39;
  indirizzi 0x38 e 0x6B). Riusa quel bus, non aprirne un altro sugli stessi pin.
- **GPIO liberi** per periferiche tue: 2, 3, 4, 10–16, 21, 38. Evita 26 e 33–37 (PSRAM octal).

## Esempio incluso: examples/Orientation_IMU

Sketch autosufficiente che usa l'IMU onboard QMI8658 per mostrare un indicatore
di assetto: una linea di orizzonte che si inclina con il roll e scorre con il
pitch, piu' i valori numerici. La UI e' costruita in codice (non con SquareLine).

- Apri `examples/Orientation_IMU/Orientation_IMU.ino` in Arduino IDE e compila.
- L'IMU e' sullo stesso bus I2C del touch (gia' avviato da `lvgl_port_init()`),
  quindi il modulo `imu_qmi8658` non reinstalla il driver I2C.
- Pitch/roll derivano dall'accelerometro (lo yaw non e' osservabile dal solo
  accelerometro). Se gli assi risultano invertiti, cambia i segni nelle due righe
  indicate nel `loop()`. Se l'IMU non risponde, prova l'indirizzo 0x6A in
  `imu_qmi8658.c` (default 0x6B).
