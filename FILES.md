# FILES.md — Dettaglio file per file

Reference completa di ogni file sorgente del repo: scopo, contenuto, dipendenze,
cosa NON toccare. Per il pinout/hardware della board vedi
`ESP32-S3-AMOLED-1.91-Guide.md`; per l'architettura d'insieme e i comandi di build
vedi `CLAUDE.md`. Qui il livello è quello del singolo file.

`WSOLED/` ed `examples/Orientation_IMU/` condividono 8 file **byte-identici**
(`lvgl_port.*`, `esp_lcd_sh8601.*`, `touch_bsp.*`, `lv_conf.h`, `build_opt.h`) — sono
copie indipendenti per vincolo Arduino (ogni sketch deve avere tutti i suoi file
nella propria cartella), non un modulo condiviso. Sono descritti una volta sola,
sotto `WSOLED/`.

---

## `WSOLED/` — il template

### `WSOLED.ino`

**Ruolo**: sketch principale, punto di ingresso Arduino (`setup()`/`loop()`). È
l'unico file pensato per essere riscritto ad ogni nuovo progetto copiato dal
template.

- `setup()`: chiama `lvgl_port_init()` (hardware pronto), poi `ui_init()` sotto
  `lvgl_lock()` (crea gli oggetti LVGL), poi lascia spazio per WiFi/SD/sensori
  (commentati come placeholder: `#include <WiFi.h>` / `<SD.h>`).
- `loop()`: esempio di pattern periodico (`millis()` ogni 1000ms) che legge un
  sensore fittizio (`read_sensor()`, ritorna sempre `23.5f` — da sostituire) e
  aggiornerebbe una label LVGL sotto lock (riga commentata,
  `lv_label_set_text(ui_LabelTemp, buf)`, perché lo stub non ha ancora quell'oggetto).
  Chiude con `delay(5)`.
- Commento in testa ribadisce la regola del lock e le impostazioni Tools richieste
  (Board, Flash, Partizione, PSRAM, USB CDC).

**Dipendenze**: `lvgl_port.h`, `ui.h`.
**Da sapere**: il nome del file **deve** coincidere col nome della cartella
(vincolo Arduino) — è il primo blocco quando si copia il template.

---

### `lvgl_port.h` / `lvgl_port.cpp`

**Ruolo**: modulo di porting — tutto l'hardware (display QSPI, touch I2C, LVGL,
task di rendering, mutex) in un unico posto. **Non si tocca** per lavoro
applicativo normale.

**API pubblica (`lvgl_port.h`)**:
- `lvgl_port_init(void)` — init completa, da chiamare una volta in `setup()`.
- `lvgl_lock(int timeout_ms)` / `lvgl_unlock(void)` — mutex FreeRTOS obbligatorio
  attorno a ogni accesso LVGL fuori dal task di rendering. `timeout_ms = -1` =
  attesa infinita.
- `lcd_command(uint8_t cmd, const uint8_t *data, size_t len)` — invia un comando
  raw al pannello via QSPI, gestendo internamente il framing (vedi sotto).
- `lcd_set_brightness(uint8_t level)` — scorciatoia per il registro `0x51`.
- `lcd_read_register(uint8_t cmd, uint8_t *data, size_t len)` — lettura registro
  via QSPI (opcode `0x03`), meno affidabile della scrittura su questo pannello.

**Implementazione (`lvgl_port.cpp`)**:
- Pin display: CS=6, PCLK=47, DATA0-3=18/7/48/5, RST=17 (QSPI, `SPI2_HOST`),
  risoluzione 536×240, 16 bit/pixel.
- `lcd_init_cmds[]`: sequenza di init del pannello SH8601 — Sleep Out (`0x11`),
  MADCTL landscape (`0x36`=`0xF0`, **valore confermato funzionante**, non usare
  `0x00`), pixel format RGB565 (`0x3A`=`0x55`), WRCTRLD/BCTRL abilitato
  (`0x53`=`0x20`, **necessario** perché il comando brightness `0x51` abbia
  effetto), finestra colonne/righe (`0x2A`/`0x2B`), Display On (`0x29`),
  brightness massima (`0x51`=`0xFF`).
- `lcd_command()`: costruisce manualmente il framing QSPI —
  `(0x02 << 24) | (cmd << 8)` — perché questo framing è applicato solo
  internamente dal driver vendor (`esp_lcd_sh8601.c`, funzione statica non
  esposta) per le sue operazioni interne; per comandi ad-hoc a runtime va
  replicato a mano.
- `lvgl_flush_cb()` / `flush_ready_cb()`: callback standard LVGL↔esp_lcd
  (draw bitmap / segnala frame consegnato).
- `lvgl_touch_cb()`: legge `getTouch()` da `touch_bsp` e lo mappa su
  `lv_indev_data_t`.
- `lvgl_tick_cb()`: timer `esp_timer` periodico ogni 2ms, chiama `lv_tick_inc()`
  (coerente con `LV_TICK_CUSTOM 0` in `lv_conf.h` — il tick è manuale, non
  automatico).
- Buffer di disegno: doppio buffer DMA (`heap_caps_malloc(..., MALLOC_CAP_DMA)`),
  `LVGL_BUF_LINES = LCD_V_RES/4` = 60 righe ciascuno. Commento nel file: con WiFi
  attivo la RAM interna è preziosa, scendere a `/8` se serve liberare memoria.
- `lvgl_task()`: task FreeRTOS dedicato (stack 4KB, priorità 2) che in loop prende
  il lock, chiama `lv_timer_handler()`, rilascia il lock, e dorme per il delay
  ritornato (clampato tra `LVGL_TASK_MIN_DELAY_MS`=1 e `LVGL_TASK_MAX_DELAY_MS`=500).
- `lvgl_port_init()`: sequenza completa — bus QSPI → panel IO → driver SH8601 →
  `Touch_Init()` → `lv_init()` → buffer/display driver → tick timer → input
  device touch → mutex → avvio task di rendering.

**Da sapere**: bus QSPI a 40MHz (`SH8601_PANEL_IO_QSPI_CONFIG`, `pclk_hz`), valore
prudente — i demo vendor arrivano fino a 75MHz ma tearing/corruzione vanno
indagati abbassando la frequenza prima di sospettare il codice di disegno.

---

### `esp_lcd_sh8601.h` / `esp_lcd_sh8601.c`

**Ruolo**: driver vendor Espressif/Waveshare per il controller pannello SH8601
(usato per pilotare il RM67162 fisico — le due sigle non coincidono, vedi
`ESP32-S3-AMOLED-1.91-Guide.md` §6.1). File di libreria, non applicativo:
**non si modifica** se non per bug fix da propagare anche nell'altra copia
(`examples/Orientation_IMU/`).

**Header pubblico (`.h`)**:
- `sh8601_lcd_init_cmd_t` — struct comando init (`cmd`, `data`, `data_bytes`,
  `delay_ms`), usata per personalizzare la sequenza di init via `vendor_config`.
- `sh8601_vendor_config_t` — passa `init_cmds`/`init_cmds_size` custom e il flag
  `use_qspi_interface`.
- `esp_lcd_new_panel_sh8601()` — factory del pannello, stile `esp_lcd` standard.
- Macro `SH8601_PANEL_BUS_QSPI_CONFIG` / `SH8601_PANEL_IO_QSPI_CONFIG` (e varianti
  SPI non-quad, non usate su questa board) — precompilano `spi_bus_config_t` /
  `esp_lcd_panel_io_spi_config_t` con i default corretti per questo controller
  (`lcd_cmd_bits=32`, `lcd_param_bits=8`, `quad_mode=true`).

**Implementazione (`.c`)**:
- `tx_param()` / `tx_color()` — helper **statici** (non esposti) che applicano il
  framing QSPI: `lcd_cmd = (opcode << 24) | ((cmd & 0xFF) << 8)`, opcode `0x02`
  per comandi (`tx_param`) e `0x32` per colore (`tx_color`). Questo è il framing
  che `lvgl_port.cpp::lcd_command()` deve replicare manualmente per i comandi
  a runtime, perché queste funzioni non sono richiamabili dall'esterno del file.
- `panel_sh8601_init()`: manda MADCTL/COLMOD di default (calcolati da
  `rgb_ele_order`/`bits_per_pixel` passati in config), poi la sequenza
  `init_cmds` fornita (quella di `lvgl_port.cpp`) — se questa ridefinisce
  MADCTL/COLMOD, logga un warning ma applica comunque l'override.
  Senza `init_cmds` custom userebbe `vendor_specific_init_default[]` (sequenza
  minimale interna, diversa da quella di questo progetto).
- `panel_sh8601_draw_bitmap()`: invia `CASET`/`RASET` (finestra x/y) prima di
  ogni frame via `tx_param`, poi il framebuffer via `tx_color` — per questo la
  sequenza di init non ha bisogno di preimpostare la finestra colonne/righe in
  modo permanente.
- `panel_sh8601_mirror()`: supporta mirror X, **non** mirror Y (ritorna
  `ESP_ERR_NOT_SUPPORTED` con log di errore se richiesto).
- `panel_sh8601_swap_xy()`: **non supportato** da questo pannello (ritorna
  sempre errore) — l'orientazione si gestisce solo con MADCTL in init, non con
  rotazione software di questo driver.

**Da sapere**: non è farina di questo progetto (SPDX Espressif, Apache-2.0) — va
trattato come libreria vendor upstream.

---

### `touch_bsp.h` / `touch_bsp.c`

**Ruolo**: driver touch FT3168 su I2C. Piccolo, scritto ad-hoc (non un componente
Espressif ufficiale).

**Header (`.h`)**: `Touch_Init(void)`, `getTouch(uint16_t *x, uint16_t *y)` →
`1` se c'è un tocco, `0` altrimenti.

**Implementazione (`.c`)**:
- Bus `I2C_NUM_0`, SDA=GPIO40, SCL=GPIO39, clock 300kHz, indirizzo FT3168=`0x38`.
- `Touch_Init()`: configura e installa il driver I2C (`i2c_param_config` +
  `i2c_driver_install`), poi scrive `0x00` al registro `0x00` (modalità normale).
  **Questo è il modulo che possiede il bus I2C**: chiunque altro lo usi (es.
  `imu_qmi8658.c`) deve riusarlo, non reinstallarlo.
- `getTouch()`: legge registro `0x02` (numero di tocchi); se >0 legge 4 byte dal
  registro `0x03` — **Y nei primi 2 byte, X nei successivi 2** (ordine invertito
  rispetto a quanto ci si aspetterebbe), clampa a 536×240, poi fa
  `*y = 240 - *y` (flip verticale — necessario perché il controller conta Y al
  contrario rispetto al sistema di coordinate del pannello/LVGL).
- `I2C_writr_buff()` / `I2C_read_buff()` / `I2C_master_write_read_device()`:
  helper I2C generici usati internamente (nome `writr` con refuso, non
  `write` — lasciato così, è cosmetico).

**Da sapere**: nessun uso dell'interrupt touch (GPIO41, vedi Guide.md) — questo
driver fa polling puro tramite `lvgl_touch_cb()` in `lvgl_port.cpp`. `TP_RST` non
è gestito qui perché è fisicamente legato al reset del pannello (vedi Guide.md §7).

---

### `lv_conf.h`

**Ruolo**: configurazione LVGL **a livello di progetto** (letta grazie a
`LV_CONF_INCLUDE_SIMPLE` in `build_opt.h`). È sostanzialmente il template
default di LVGL 8.3.11 con pochi valori adattati a questa board — **non è stata
sfoltita** per risparmiare RAM/flash: quasi tutti i widget ed extra sono
abilitati (`=1`).

Valori significativi/non-default:
- `LV_COLOR_DEPTH 16` + `LV_COLOR_16_SWAP 1` — RGB565 con byte-swap, richiesto
  dall'interfaccia QSPI di questo pannello.
- `LV_TICK_CUSTOM 0` — il tick NON è automatico: coerente con
  `lvgl_tick_cb()`/`lv_tick_inc()` chiamato a mano ogni 2ms in `lvgl_port.cpp`.
- `LV_MEM_SIZE (48*1024)` — 48KB di heap interno LVGL (allocazione statica, RAM
  interna, non PSRAM). Separato dai buffer di disegno DMA allocati a parte in
  `lvgl_port.cpp`.
- `LV_DISP_DEF_REFR_PERIOD` / `LV_INDEV_DEF_READ_PERIOD` = 30ms — periodo interno
  di LVGL per invalidazione/lettura input; il task di rendering vero e proprio
  (`lvgl_task` in `lvgl_port.cpp`) ha la sua logica di delay indipendente.
- `LV_FONT_DEFAULT &lv_font_montserrat_14`, con Montserrat 12/14/16 abilitati
  (altre taglie disattivate).
- `LV_USE_THEME_DEFAULT 1`, `LV_THEME_DEFAULT_DARK 0` → tema chiaro di default
  finché una UI SquareLine non lo sovrascrive esplicitamente.
- `LV_USE_FS_*` tutti a `0` — nessun filesystem collegato a LVGL. Se in futuro si
  vuole caricare immagini/font da microSD via widget LVGL (`lv_img_set_src` con
  path), va abilitato e configurato `LV_USE_FS_FATFS` (driver più comune per SD
  su ESP32) — non è ancora stato fatto in questo template.
- `LV_BUILD_EXAMPLES 1`, `LV_USE_DEMO_WIDGETS 1`, `LV_USE_DEMO_MUSIC 1` — compila
  anche le demo integrate di LVGL nella libreria (non richiamate da nessun file
  di questo progetto, quindi codice morto a meno di chiamarle esplicitamente;
  occupano flash ma con margine ampio — vedi `CLAUDE.md` per i numeri di
  compilazione).

**Da sapere**: se si cambia `LV_COLOR_DEPTH`/`LV_COLOR_16_SWAP` bisogna verificare
che combaci ancora con `bits_per_pixel`/`rgb_ele_order` passati a
`esp_lcd_new_panel_sh8601()` in `lvgl_port.cpp`, e con le impostazioni del
progetto SquareLine (deve restare 16 bit).

---

### `build_opt.h`

**Ruolo**: singola riga di flag globali passati dall'IDE Arduino al compilatore
per **tutti** i file dello sketch:

```
-DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE
```

- `LV_CONF_INCLUDE_SIMPLE`: LVGL cerca `lv_conf.h` lungo l'include path (quello di
  progetto) invece che a percorso relativo fisso — è quello che fa funzionare il
  `lv_conf.h` di questa cartella.
- `LV_LVGL_H_INCLUDE_SIMPLE`: i file generati da SquareLine includono `lvgl.h` in
  modo "semplice" (`#include "lvgl.h"` invece di percorso relativo) — senza
  questo, dopo un export SquareLine si ottengono errori `lvgl.h: No such file`.

**Da sapere**: riconosciuto solo da Arduino IDE (meccanismo `build_opt.h`
specifico dei sketch), non da `arduino-cli` in automatico a meno che il file sia
nella cartella dello sketch — è lì, quindi `arduino-cli compile` lo applica
correttamente (verificato: compilazione riuscita, vedi `CLAUDE.md`).

---

### `ui.h` / `ui.c`

**Ruolo**: **stub segnaposto**, destinato a essere interamente sovrascritto
dall'export "Export UI Files" di SquareLine Studio.

- `ui.h`: dichiara solo `void ui_init(void)` — lo stesso contratto che genera
  SquareLine, così il resto del progetto (`WSOLED.ino`) non deve cambiare quando
  arriva l'export vero.
- `ui.c`: crea una singola label centrata con testo "Starter pronto" — serve solo
  a confermare che display/touch/LVGL funzionano prima di disegnare la UI reale.

**Da sapere**: dopo un export SquareLine, questi due file vengono sostituiti (non
uniti) — arrivano anche `ui_helpers.*`, `ui_events.*`, gli screen e gli asset.
`ui_events.c` (quando esiste) **non** viene sovrascritto ai re-export successivi,
quindi è il posto giusto per la logica degli eventi widget.

---

## `examples/Orientation_IMU/` — demo autosufficiente

Contiene copie identiche di `lvgl_port.*`, `esp_lcd_sh8601.*`, `touch_bsp.*`,
`lv_conf.h`, `build_opt.h` (vedi sopra) più questi file specifici della demo:

### `Orientation_IMU.ino`

**Ruolo**: sketch completo e funzionante — livella per camper basato sull'IMU
onboard. UI costruita interamente in codice (non SquareLine), utile come esempio
di pattern alternativo a `ui.c`/SquareLine.

- Dimensioni camper hardcoded in testa (`TRACK_MM`, `WHEELBASE_MM` — Adria Matrix
  Axess 680 SP / Fiat Ducato Maxi) — **da adattare** per un altro veicolo; la
  direzione (segni di roll/pitch) resta corretta comunque, solo i centimetri
  dipendono dalle dimensioni reali.
- `leveling_ui_create()`: costruisce la UI sotto lock in `setup()` — pianta
  camper con 4 "ruote" colorate (verde/ambra/rosso in base al rialzo necessario),
  bolla centrale che si sposta, pannello testo con pitch/roll, bottone CALIBRA.
- `leveling_update(pitch, roll)`: converte pitch/roll in cm di rialzo per ruota
  (trigonometria su `TRACK_MM`/`WHEELBASE_MM`), aggiorna colori/testi/posizione
  bolla.
- `loop()`: legge l'IMU ogni 20ms (media di 8 campioni per ridurre rumore),
  filtro esponenziale lento (costante 0.05) su pitch/roll filtrati, aggiorna la
  UI ogni 300ms sotto lock. Calibrazione (`s_calibrate_req`, impostato dal
  bottone) azzera l'offset corrente.
- Roll/pitch calcolati da sola accelerazione (`atan2f`) — **nessuno yaw**
  possibile con solo accelerometro; se gli assi risultano invertiti sul tuo
  mezzo, i punti da modificare sono commentati esplicitamente nel `loop()`.

**Dipendenze**: `lvgl.h`, `lvgl_port.h`, `imu_qmi8658.h`.

---

### `imu_qmi8658.h` / `imu_qmi8658.c`

**Ruolo**: mini-driver per l'IMU onboard QMI8658 (solo accelerometro, non
gyro) — esiste solo in questa demo, non nel template `WSOLED/`.

**Header**: `imu_init(void)` → `false` se il chip non risponde;
`imu_read_accel(float *ax, float *ay, float *az)` → valori in g.

**Implementazione**:
- Bus `I2C_NUM_0` **condiviso** col touch — non reinstalla il driver I2C
  (riusa quello di `Touch_Init()`, già avviato da `lvgl_port_init()` prima che
  `imu_init()` venga chiamato in `setup()`).
- Indirizzo `0x6B` (commento: "se WHO_AM_I fallisce, prova 0x6A" — **non
  implementato come fallback automatico nel codice**, è solo una nota per debug
  manuale; vedi anche la nota di verifica in `ESP32-S3-AMOLED-1.91-Guide.md` §8,
  che invece indica 0x6B come l'unico indirizzo atteso su questa board).
- `imu_init()`: legge `WHO_AM_I` (registro `0x00`, atteso `0x05`), poi scrive
  `CTRL1=0x40` (auto-increment per lettura a blocco — **valore diverso** da
  quello suggerito nella guida hardware, `0x60`; non ri-verificato a livello di
  bit contro il datasheet, ma questo è il valore usato dalla demo funzionante),
  `CTRL2=0x05` (accelerometro ±2g, ODR ~250Hz), `CTRL7=0x01` (abilita solo
  l'accelerometro), poi 20ms di attesa.
- `imu_read_accel()`: legge 6 byte da `0x35` (`QMI_AX_L`), converte i 3
  `int16_t` con sensibilità `1/16384` g/LSB (fondo scala ±2g).

**Da sapere**: nessuna gestione degli interrupt IMU (GPIO45/46) — solo polling
dal `loop()` dello sketch.

---

## File a livello repository

### `README.md`

Introduzione e istruzioni d'uso del template in italiano: tabella file, setup
Arduino IDE, spiegazione `build_opt.h`, procedura "avvia un nuovo progetto",
dove scrivere la logica applicativa, note hardware (SD condivisa, I2C condiviso,
GPIO liberi), descrizione dell'esempio incluso. È il documento rivolto a un
umano che apre il repo per la prima volta.

### `CLAUDE.md`

Guida operativa per lavorare sul repo con Claude Code: comandi di build/verifica
(incluso `arduino-cli`), architettura di `lvgl_port`, workflow SquareLine, vincoli
hardware, convenzioni (dove scrivere la logica, gestione del lock). Non ripete il
dettaglio file-per-file (quello è qui, in `FILES.md`) né il pinout completo
(quello è in `ESP32-S3-AMOLED-1.91-Guide.md`).

### `ESP32-S3-AMOLED-1.91-Guide.md`

Guida di riferimento hardware/board-level (non file-per-file): identità scheda,
pinout completo, pin da non toccare, toolchain, flashing/boot mode, dettagli
display/touch/IMU/SD/batteria/WiFi, checklist di errori comuni. Cross-verificata
col codice di questo repo (vedi commit `b8a7fd8`): gli snippet ora riflettono i
valori confermati funzionanti (framing QSPI, MADCTL, WRCTRLD, flip Y touch), con
note esplicite dove non è stato possibile verificare con certezza (revisione SD
V1/V2, valore CTRL1 IMU).

### `.gitignore`

Esclude artefatti di build Arduino (`build/`, `*.bin`, `*.elf`, `*.map`), file di
sistema Windows/macOS, `.vscode/`, e `.claude/settings.local.json` (permessi
locali di Claude Code per questa macchina/sessione, non da condividere).

### `.claude/settings.local.json`

Configurazione locale di Claude Code (permessi Bash consentiti in questa
sessione) — **non versionata** (vedi `.gitignore`), specifica di questa macchina.

### PDF di riferimento (root)

`ESP32-S3-AMOLED-1.91.pdf` (schema board), `esp32-s3_datasheet_en.pdf`,
`esp32-s3_technical_reference_manual_en.pdf`, `RM67162.pdf`, `QMI8658C_datasheet_rev_0.9.pdf`,
`Guida_LVGL_SquareLine_ESP32S3_AMOLED.pdf` — datasheet e guide sorgente da cui è
stata derivata `ESP32-S3-AMOLED-1.91-Guide.md`. Consultarli per dettagli di
registro/timing non coperti dalla guida; non riscriverne il contenuto nel codice
o in altra documentazione.
