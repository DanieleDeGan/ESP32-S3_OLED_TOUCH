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

Per il dettaglio file-per-file (scopo, funzioni chiave, dipendenze, cosa non
toccare) vedi `FILES.md`. Per il pinout/hardware della board vedi
`ESP32-S3-AMOLED-1.91-Guide.md`.

## Struttura

| Percorso | Ruolo |
|---|---|
| `libraries/` | librerie Arduino condivise (bus/display/touch/IMU), vedi sotto |
| `WSOLED/` | il template: sketch vuoto + logica applicativa |
| `WSOLED/WSOLED.ino` | sketch principale — `setup()`/`loop()`, qui va SOLO la logica applicativa |
| `WSOLED/lv_conf.h` | configurazione LVGL a livello di progetto |
| `WSOLED/build_opt.h` | flag di compilazione globali (vedi sotto) |
| `WSOLED/ui.h/.c` | stub segnaposto, sostituiti dall'export "UI Files" di SquareLine |
| `examples/Orientation_IMU/` | demo autosufficiente: livello a bolla per camper basato sull'IMU onboard, UI costruita in codice (non SquareLine) |
| `*.pdf` (root) | datasheet/reference (ESP32-S3, SH8601/RM67162, QMI8658, guida LVGL+SquareLine) — consultarli per dettagli di registro/timing, non riscriverne il contenuto nel codice |

### `libraries/` — le librerie Arduino condivise

Il boilerplate hardware (display, touch, IMU, bus I2C) vive in quattro librerie
Arduino locali, una per periferica, così ogni sketch include solo quello che
usa:

| Libreria | Ruolo | API pubblica |
|---|---|---|
| `WSOLED_Core` | bring-up idempotente del bus I2C condiviso | `Core_I2CBusInit()` |
| `WSOLED_Display` | pannello SH8601 (QSPI) + LVGL + task di rendering + mutex — **non si tocca** | `Display_Init()`, `lvgl_lock()`/`lvgl_unlock()`, `lcd_command()`/`lcd_set_brightness()`/`lcd_read_register()` |
| `WSOLED_Touch` | driver touch FT3168 su I2C, con wiring LVGL opzionale | `Touch_Init()`, `getTouch()`, `Touch_RegisterLvglIndev()` |
| `WSOLED_IMU` | mini-driver IMU QMI8658 onboard | `imu_init()`, `imu_read_accel()` |

`WSOLED/` ed `examples/Orientation_IMU/` condividono queste librerie (nessuna
copia duplicata del codice hardware: un bug fix in una libreria vale per tutti
gli sketch che la includono). Restano invece deliberatamente **per-sketch**
(non condivisi) `lv_conf.h` e `build_opt.h`, perché sono configurazione di
progetto, non codice — vedi le rispettive sezioni sotto.

Le librerie sono referenziate da `arduino-cli` con `--libraries libraries`
(vedi comandi sotto) e, per l'uso in Arduino IDE, tramite junction Windows già
create in `C:\Users\<utente>\Documents\Arduino\libraries\WSOLED_*` che
puntano alla cartella `libraries/` di questo repo (nessuna copia manuale,
Developer Mode non necessario: le junction non richiedono i permessi dei
symlink).

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
arduino-cli compile --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,CPUFreq=240,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi" --libraries libraries WSOLED
arduino-cli compile --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,CPUFreq=240,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi" --libraries libraries examples/Orientation_IMU
```

`--libraries libraries` (percorso relativo, da eseguire dalla radice del repo)
fa risolvere a `arduino-cli` le quattro librerie locali in `libraries/`. Non è
strettamente necessario su questa macchina — esistono anche le junction verso
`Documents/Arduino/libraries/WSOLED_*` (vedi sopra), che arduino-cli/l'IDE
trovano comunque di default — ma è il modo esplicito e portabile di puntarci,
utile anche su un'altra macchina senza junction già create.

Equivalente via Arduino IDE (Tools menu):
- Board: **ESP32S3 Dev Module**
- Flash Size: **16MB**
- Partition Scheme: **16M Flash (3MB APP/9.9MB FATFS)**
- PSRAM: **OPI PSRAM**
- USB CDC On Boot: **Enabled**
- CPU Frequency: **240 MHz**

Per caricare su scheda reale (non solo verificare) serve `--upload -p <porta_seriale>`,
non testato da qui in quanto richiede la scheda collegata.

Dipendenze: libreria esterna **LVGL 8.3.x** (via Library Manager, invariata)
più le quattro librerie locali `WSOLED_Core`/`WSOLED_Display`/`WSOLED_Touch`/
`WSOLED_IMU` in `libraries/` (driver pannello, touch, IMU e bus I2C
condiviso) — vedi la sezione `libraries/` sopra.

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

## Architettura delle librerie condivise (`libraries/`)

Ordine di init in `setup()` (vedi `WSOLED.ino`/`Orientation_IMU.ino`):

1. **`Display_Init()`** (`WSOLED_Display`) — fa tutto l'init display in un colpo
   solo: bus QSPI → panel IO → driver SH8601 → `lv_init()` → buffer di disegno
   DMA doppi (`LVGL_BUF_LINES` righe ciascuno) → display driver → tick timer
   (`esp_timer`, 2ms) → mutex → **task FreeRTOS dedicato** che chiama
   `lv_timer_handler()` in loop. **Nessuna dipendenza dal touch.**
2. **`Touch_Init()`** (`WSOLED_Touch`, opzionale) — chiama internamente
   `Core_I2CBusInit()` poi configura il touch FT3168.
3. **`Touch_RegisterLvglIndev()`** (`WSOLED_Touch`, opzionale, dopo
   `Display_Init()`) — registra il touch come input device LVGL. Il task di
   rendering può già essere partito a questo punto (gira potenzialmente
   sull'altro core): la registrazione avvolge `lv_indev_drv_register()` in
   `lvgl_lock()`/`lvgl_unlock()` internamente, quindi è sicura da chiamare
   in qualunque momento dopo `Display_Init()`.
4. **`imu_init()`** (`WSOLED_IMU`, opzionale) — chiama internamente
   `Core_I2CBusInit()` poi configura la IMU QMI8658. Funziona indipendentemente
   dal fatto che `Touch_Init()` sia stato chiamato prima, dopo, o per niente
   (entrambi passano dallo stesso `Core_I2CBusInit()` idempotente).

**Regola fondamentale di threading**: il rendering LVGL gira nel suo task. Qualunque
accesso a un oggetto LVGL da un contesto diverso (`loop()`, task sensori/WiFi propri,
callback) deve essere avvolto in `lvgl_lock(-1)` / `lvgl_unlock()` (esportate da
`WSOLED_Display`). Dentro un callback di evento LVGL il lock è già acquisito: non
ri-prenderlo, e tenere il callback corto (lavoro lento come SD/rete va deferito a
`loop()`/un task).

`lcd_command()` / `lcd_set_brightness()` / `lcd_read_register()` (`WSOLED_Display`)
parlano direttamente col pannello via QSPI (stesso bus del rendering): se chiamate a
runtime dal `loop()`/da un task, vanno anch'esse avvolte nel lock; non serve dentro
l'init o dentro una callback LVGL (lock già preso, o task non ancora avviato).

**Aggiungere un nuovo modulo (es. SD/WiFi) in futuro**: segui lo stesso schema —
libreria propria in `libraries/WSOLED_<Nome>/`, `library.properties` senza
campo `depends` (l'unico meccanismo di risoluzione che conta con `--libraries`
è l'`#include` letterale, non quel campo), e se serve il bus I2C/QSPI condiviso
passa da `WSOLED_Core`/i primitivi già esposti da `WSOLED_Display` invece di
reinstallare bus propri. Nessun modulo di questo repo avvia un task FreeRTOS
proprio a parte `WSOLED_Display`: valutalo solo per moduli con lavoro continuo
in background (tipicamente WiFi/SD), non per sensori a lettura rapida come
Touch/IMU, che restano volutamente passivi (letti da `loop()`/dal task LVGL).

## Workflow SquareLine Studio (per `WSOLED/`, template)

1. Nuovo progetto SquareLine: risoluzione **536×240**, colore **16 bit**, LVGL
   **8.3.x** (deve combaciare con `lv_conf.h` e la libreria installata).
2. **"Export UI Files"** con percorso di export = la cartella dello sketch. Questo
   sovrascrive `ui.h`/`ui.c` e porta anche `ui_helpers.*`, `ui_events.*`, screen e
   asset.
3. **Non** usare il `.ino` né il driver TFT_eSPI generati da SquareLine: il display è
   già gestito da `WSOLED_Display`. Si tengono solo i file `ui_*`.
4. Il corpo degli eventi "Call function" definiti in SquareLine va in `ui_events.c`
   (non viene sovrascritto ai re-export).
5. Se l'IDE non vede i file appena aggiunti dopo un export, chiuderlo e riaprirlo (la
   build cache mantiene lo stato precedente).

## Avviare un nuovo progetto dal template

1. Copiare l'intera cartella `WSOLED/` in `MioProgetto/`.
2. **Portabilità delle librerie**: se `MioProgetto/` resta dentro questo repo
   (accanto a `WSOLED/`/`examples/`), non serve altro — condivide già
   `libraries/` alla radice. Se invece `MioProgetto/` diventa un progetto/repo
   indipendente altrove sul disco, copia (o crea una junction verso) anche la
   cartella `libraries/` accanto ad esso: senza, non compila, perché
   `WSOLED_Display`/`WSOLED_Touch`/`WSOLED_IMU`/`WSOLED_Core` non sono più
   incluse dentro la cartella dello sketch come nel vecchio schema a copie.
3. Rinominare lo sketch in `MioProgetto.ino` — il nome del `.ino` **deve** coincidere
   col nome della cartella (vincolo Arduino).
4. Compilare e caricare così com'è prima di toccare la UI, per confermare che
   display/touch/LVGL funzionino (compare "Starter pronto" centrato).
5. Poi procedere con l'export SquareLine come sopra.

## Hardware: vincoli di pinout (scheda Waveshare ESP32-S3-Touch-AMOLED-1.91)

- **Display QSPI**: CS=GPIO6, PCLK=GPIO47, DATA0-3=GPIO18/7/48/5, RST=GPIO17.
- **microSD**: condivide i pin col display (CLK=GPIO47, MISO=GPIO8, MOSI=GPIO42,
  CS=GPIO9) — **non** è un bus indipendente, seguire il demo `SD_Test` di Waveshare.
  Schede ≤ 64 GB, FAT32.
- **I2C condiviso** tra touch FT3168 (addr `0x38`) e IMU onboard QMI8658
  (addr `0x6B`, fallback `0x6A`): SDA=GPIO40, SCL=GPIO39. `Core_I2CBusInit()`
  (libreria `WSOLED_Core`) installa il driver I2C in modo idempotente: sia
  `Touch_Init()` (`WSOLED_Touch`) sia `imu_init()` (`WSOLED_IMU`) la chiamano
  internamente, quindi funzionano in qualunque ordine, anche uno senza l'altro.
- **GPIO liberi** per periferiche custom: 2, 3, 4, 10–16, 21, 38. Evitare 26 e 33–37
  (riservati alla PSRAM octal).

## Dove scrivere la logica applicativa

- Eventi dei widget SquareLine → `ui_events.c`.
- Aggiornamenti UI da `loop()`/task sensori/callback WiFi → sempre dentro
  `lvgl_lock(-1)` … `lvgl_unlock()`.
- Dentro una callback di evento LVGL → niente lock (già preso), callback corta,
  lavoro lento deferito.
