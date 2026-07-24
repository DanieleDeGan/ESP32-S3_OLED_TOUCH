# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Cos'è questo repository

Non è un singolo progetto ma uno **starter template riutilizzabile** per sviluppare
interfacce LVGL (disegnate in SquareLine Studio) sulla Waveshare
**ESP32-S3-Touch-AMOLED-1.91** (pannello AMOLED 536×240 SH8601, touch capacitivo
FT3168, IMU onboard QMI8658). Non è un progetto Arduino compilabile di per sé nel
senso classico: `WSOLED/` va **copiato** in una nuova cartella per ogni progetto reale
(vedi "Avviare un nuovo progetto" più sotto). `examples/Orientation_IMU/` è una demo
autosufficiente, indipendente dal template.

Non è (ancora) un repository git.

## Struttura

| Percorso | Ruolo |
|---|---|
| `WSOLED/` | il template: sketch vuoto + tutto il boilerplate hardware |
| `WSOLED/WSOLED.ino` | sketch principale — `setup()`/`loop()`, qui va SOLO la logica applicativa |
| `WSOLED/lvgl_port.h/.cpp` | display + touch + LVGL + task di rendering + mutex — **non si tocca** |
| `WSOLED/esp_lcd_sh8601.h/.c` | driver Espressif/Waveshare per il pannello SH8601 |
| `WSOLED/touch_bsp.h/.c` | driver touch FT3168 su I2C (Waveshare) |
| `WSOLED/lv_conf.h` | configurazione LVGL a livello di progetto |
| `WSOLED/build_opt.h` | flag di compilazione globali (vedi sotto) |
| `WSOLED/ui.h/.c` | stub segnaposto, sostituiti dall'export "UI Files" di SquareLine |
| `examples/Orientation_IMU/` | demo autosufficiente: livello a bolla per camper basato sull'IMU onboard, UI costruita in codice (non SquareLine) |
| `examples/Orientation_IMU/imu_qmi8658.h/.c` | mini-driver IMU QMI8658, esiste solo in questa demo |
| `*.pdf` (root) | datasheet/reference (ESP32-S3, SH8601/RM67162, QMI8658, guida LVGL+SquareLine) — consultarli per dettagli di registro/timing, non riscriverne il contenuto nel codice |

`examples/Orientation_IMU/` contiene una **copia indipendente e identica** di
`lvgl_port.*`, `esp_lcd_sh8601.*`, `touch_bsp.*`, `lv_conf.h`, `build_opt.h` — Arduino
richiede che ogni sketch abbia tutti i suoi file nella propria cartella, quindi non
c'è un modulo condiviso. **Se correggi un bug o migliori questi file in un posto
(es. `WSOLED/lvgl_port.cpp`), valuta se applicare la stessa modifica anche nella
copia in `examples/Orientation_IMU/`** — sono divergenti solo perché copiati a mano,
non per scelta di design.

## Build / verifica

Progetto **Arduino** (niente PlatformIO/CMake). `arduino-cli` è installato in
`C:\Program Files\Arduino CLI\arduino-cli.exe` (non ancora nel PATH di default:
usare il percorso completo, o `arduino-cli` diretto se una nuova sessione shell ha
già raccolto il PATH aggiornato da winget). Usa la stessa cartella dati di Arduino
IDE (`Arduino15`), quindi vede già **ESP32 core 3.3.10** e libreria **lvgl 8.3.11**
(via Library Manager) — versione compatibile con quanto richiesto da
`lv_conf.h`/`build_opt.h` (LVGL 8.3.x).

Compilazione di verifica da riga di comando (FQBN con le stesse opzioni del
Tools menu di Arduino IDE):

```
arduino-cli compile --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,CPUFreq=240,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi" WSOLED
arduino-cli compile --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,CPUFreq=240,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi" examples/Orientation_IMU
```

Equivalente via Arduino IDE (Tools menu):
- Board: **ESP32S3 Dev Module**
- Flash Size: **16MB**
- Partition Scheme: **16M Flash (3MB APP/9.9MB FATFS)**
- PSRAM: **OPI PSRAM**
- USB CDC On Boot: **Enabled**
- CPU Frequency: **240 MHz**

Per caricare su scheda reale (non solo verificare) serve `--upload -p <porta_seriale>`,
non testato da qui in quanto richiede la scheda collegata.

Unica dipendenza esterna: libreria **LVGL 8.3.x** (via Library Manager). Il driver
pannello e il driver touch sono già nella cartella del progetto, non sono librerie
esterne.

Non esiste una test suite automatica (è codice embedded legato all'hardware): la
verifica è "compila senza errori" + test manuale su scheda reale.

### `build_opt.h`

Contiene, passati globalmente al compilatore:
```
-DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE
```
- `LV_CONF_INCLUDE_SIMPLE` → LVGL cerca `lv_conf.h` lungo l'include path (quello di
  progetto) invece che a percorso relativo fisso.
- `LV_LVGL_H_INCLUDE_SIMPLE` → i file generati da SquareLine includono `lvgl.h` in
  modo "semplice"; senza questo, dopo l'export si ottengono errori `lvgl.h: No such
  file`.

## Architettura di `lvgl_port`

`lvgl_port_init()` fa tutto l'init hardware in un colpo solo: bus QSPI → panel IO →
driver SH8601 → `Touch_Init()` (I2C) → `lv_init()` → buffer di disegno DMA doppi
(`LVGL_BUF_LINES` righe ciascuno) → display driver → tick timer (`esp_timer`, 2ms) →
touch input device → mutex → **task FreeRTOS dedicato** che chiama
`lv_timer_handler()` in loop.

**Regola fondamentale di threading**: il rendering LVGL gira nel suo task. Qualunque
accesso a un oggetto LVGL da un contesto diverso (`loop()`, task sensori/WiFi propri,
callback) deve essere avvolto in `lvgl_lock(-1)` / `lvgl_unlock()`. Dentro un
callback di evento LVGL il lock è già acquisito: non ri-prenderlo, e tenere il
callback corto (lavoro lento come SD/rete va deferito a `loop()`/un task).

`lcd_command()` / `lcd_set_brightness()` / `lcd_read_register()` parlano
direttamente col pannello via QSPI (stesso bus del rendering): se chiamate a runtime
dal `loop()`/da un task, vanno anch'esse avvolte nel lock; non serve dentro l'init o
dentro una callback LVGL (lock già preso, o task non ancora avviato).

## Workflow SquareLine Studio (per `WSOLED/`, template)

1. Nuovo progetto SquareLine: risoluzione **536×240**, colore **16 bit**, LVGL
   **8.3.x** (deve combaciare con `lv_conf.h` e la libreria installata).
2. **"Export UI Files"** con percorso di export = la cartella dello sketch. Questo
   sovrascrive `ui.h`/`ui.c` e porta anche `ui_helpers.*`, `ui_events.*`, screen e
   asset.
3. **Non** usare il `.ino` né il driver TFT_eSPI generati da SquareLine: il display è
   già gestito da `lvgl_port`. Si tengono solo i file `ui_*`.
4. Il corpo degli eventi "Call function" definiti in SquareLine va in `ui_events.c`
   (non viene sovrascritto ai re-export).
5. Se l'IDE non vede i file appena aggiunti dopo un export, chiuderlo e riaprirlo (la
   build cache mantiene lo stato precedente).

## Avviare un nuovo progetto dal template

1. Copiare l'intera cartella `WSOLED/` in `MioProgetto/`.
2. Rinominare lo sketch in `MioProgetto.ino` — il nome del `.ino` **deve** coincidere
   col nome della cartella (vincolo Arduino).
3. Compilare e caricare così com'è prima di toccare la UI, per confermare che
   display/touch/LVGL funzionino (compare "Starter pronto" centrato).
4. Poi procedere con l'export SquareLine come sopra.

## Hardware: vincoli di pinout (scheda Waveshare ESP32-S3-Touch-AMOLED-1.91)

- **Display QSPI**: CS=GPIO6, PCLK=GPIO47, DATA0-3=GPIO18/7/48/5, RST=GPIO17.
- **microSD**: condivide i pin col display (CLK=GPIO47, MISO=GPIO8, MOSI=GPIO42,
  CS=GPIO9) — **non** è un bus indipendente, seguire il demo `SD_Test` di Waveshare.
  Schede ≤ 64 GB, FAT32.
- **I2C condiviso** tra touch FT3168 (addr `0x38`) e IMU onboard QMI8658
  (addr `0x6B`, fallback `0x6A`): SDA=GPIO40, SCL=GPIO39. `Touch_Init()` (chiamato da
  `lvgl_port_init()`) installa il driver I2C: qualunque altro modulo su questo bus
  (vedi `imu_qmi8658.c`) deve riusarlo, **non** reinstallarlo.
- **GPIO liberi** per periferiche custom: 2, 3, 4, 10–16, 21, 38. Evitare 26 e 33–37
  (riservati alla PSRAM octal).

## Dove scrivere la logica applicativa

- Eventi dei widget SquareLine → `ui_events.c`.
- Aggiornamenti UI da `loop()`/task sensori/callback WiFi → sempre dentro
  `lvgl_lock(-1)` … `lvgl_unlock()`.
- Dentro una callback di evento LVGL → niente lock (già preso), callback corta,
  lavoro lento deferito.
