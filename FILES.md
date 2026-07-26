# FILES.md — Dettaglio file per file

Reference completa di ogni file sorgente del repo: scopo, contenuto, dipendenze,
cosa NON toccare. Per il pinout/hardware della board vedi
`ESP32-S3-AMOLED-1.91-Guide.md`; per l'architettura d'insieme e i comandi di build
vedi `CLAUDE.md`. Qui il livello è quello del singolo file.

Il boilerplate hardware (display, touch, IMU, bus I2C) vive in quattro librerie
Arduino condivise sotto `libraries/` — una per periferica. `WSOLED/` ed
`examples/Orientation_IMU/` le includono entrambe, senza copie duplicate del
codice: un bug fix in una libreria vale per tutti gli sketch che la usano.
Restano invece deliberatamente per-sketch (non condivisi) `lv_conf.h` e
`build_opt.h`, perché sono configurazione di progetto, non codice — vedi le
rispettive sezioni sotto `WSOLED/`.

---

## `libraries/` — le librerie Arduino condivise

### `WSOLED_Core` — `WSOLED_Core.h` / `WSOLED_Core.c`

**Ruolo**: unica libreria che non esisteva prima del refactor in librerie.
Possiede il bring-up del bus I2C condiviso tra touch e IMU.

**Header (`.h`)**: costanti `WSOLED_CORE_I2C_PORT`/`_SDA`/`_SCL`/`_CLOCK_HZ`
(I2C_NUM_0, GPIO40/39, 300kHz); `void Core_I2CBusInit(void)`.

**Implementazione (`.c`)**: `Core_I2CBusInit()` è **idempotente** (guardia
`static bool`): `i2c_param_config`+`i2c_driver_install` vengono eseguiti una
sola volta, indipendentemente da quale modulo la chiama per primo. Sia
`Touch_Init()` (`WSOLED_Touch`) sia `imu_init()` (`WSOLED_IMU`) la chiamano
internamente all'inizio — funzionano quindi in qualunque ordine, o anche uno
senza l'altro (prima di questo refactor, `imu_qmi8658.c` assumeva
implicitamente che `Touch_Init()` avesse già installato il driver I2C: un
IMU-senza-Touch avrebbe fallito silenziosamente).

**File `.c` non `.cpp`**: necessario perché l'inizializzatore designato
`.master.clk_speed = ...` di `i2c_config_t` (union anonima annidata, estensione
GNU) non è supportato dal compilatore in modalità C++; in C funziona come nel
resto del codice touch/IMU di origine.

**Da sapere**: la guardia di idempotenza non è protetta da mutex — sicura per
ogni uso reale in questo repo (`Touch_Init()`/`imu_init()` sono chiamate in
sequenza da `setup()`, single-threaded, prima che parta qualunque task). Se in
futuro un modulo la chiamasse da un task in background concorrente con
`setup()`, va aggiunta una protezione prima di riusarla in quel contesto.

---

### `WSOLED_Display` — `WSOLED_Display.h` / `.cpp` (+ `esp_lcd_sh8601.h`/`.c`)

**Ruolo**: modulo di porting — bus QSPI, pannello SH8601, LVGL, task di
rendering, mutex, in un'unica libreria. **Non si tocca** per lavoro
applicativo normale. Nessuna dipendenza (di compilazione né runtime) da
`WSOLED_Touch`.

**API pubblica (`WSOLED_Display.h`)**:
- `Display_Init(void)` — init completa, da chiamare una volta in `setup()`,
  prima di `Touch_Init()`/`Touch_RegisterLvglIndev()` se presenti.
- `lvgl_lock(int timeout_ms)` / `lvgl_unlock(void)` — mutex FreeRTOS
  obbligatorio attorno a ogni accesso LVGL fuori dal task di rendering.
  `timeout_ms = -1` = attesa infinita.
- `lcd_command(uint8_t cmd, const uint8_t *data, size_t len)` — invia un
  comando raw al pannello via QSPI, gestendo internamente il framing.
- `lcd_set_brightness(uint8_t level)` — scorciatoia per il registro `0x51`.
- `lcd_read_register(uint8_t cmd, uint8_t *data, size_t len)` — lettura
  registro via QSPI (opcode `0x03`), meno affidabile della scrittura su
  questo pannello.

**Implementazione (`WSOLED_Display.cpp`, ex `lvgl_port.cpp`)**:
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
- `lvgl_tick_cb()`: timer `esp_timer` periodico ogni 2ms, chiama `lv_tick_inc()`
  (coerente con `LV_TICK_CUSTOM 0` in `lv_conf.h` — il tick è manuale, non
  automatico).
- Buffer di disegno: doppio buffer DMA (`heap_caps_malloc(..., MALLOC_CAP_DMA)`),
  `LVGL_BUF_LINES = LCD_V_RES/4` = 60 righe ciascuno. Commento nel file: con WiFi
  attivo la RAM interna è preziosa, scendere a `/8` se serve liberare memoria.
- `lvgl_task()`: task FreeRTOS dedicato (stack 4KB, priorità 2) che in loop prende
  il lock, chiama `lv_timer_handler()`, rilascia il lock, e dorme per il delay
  ritornato (clampato tra `LVGL_TASK_MIN_DELAY_MS`=1 e `LVGL_TASK_MAX_DELAY_MS`=500).
- `Display_Init()`: sequenza completa — bus QSPI → panel IO → driver SH8601 →
  `lv_init()` → buffer/display driver → tick timer → mutex → avvio task di
  rendering. **Non chiama più `Touch_Init()`** (era l'unico punto di
  accoppiamento col touch nel vecchio `lvgl_port_init()` monolitico): il task
  viene creato come ultimo passo, quindi qualunque indev registrato *dopo*
  (es. da `Touch_RegisterLvglIndev()`) deve avvolgere la registrazione nel
  lock — vedi `WSOLED_Touch`.

**Da sapere**: bus QSPI a 40MHz (`SH8601_PANEL_IO_QSPI_CONFIG`, `pclk_hz`), valore
prudente — i demo vendor arrivano fino a 75MHz ma tearing/corruzione vanno
indagati abbassando la frequenza prima di sospettare il codice di disegno.

**`esp_lcd_sh8601.h` / `.c`** — driver vendor Espressif/Waveshare per il
controller pannello SH8601 (usato per pilotare il RM67162 fisico — le due
sigle non coincidono, vedi `ESP32-S3-AMOLED-1.91-Guide.md` §6.1), spostato
qui invariato. File di libreria, non applicativo: **non si modifica** se non
per bug fix (non c'è più una seconda copia da tenere allineata: prima del
refactor esisteva anche in `examples/Orientation_IMU/`, ora è un file solo).

Header pubblico: `sh8601_lcd_init_cmd_t`, `sh8601_vendor_config_t`,
`esp_lcd_new_panel_sh8601()`, macro `SH8601_PANEL_BUS_QSPI_CONFIG`/
`SH8601_PANEL_IO_QSPI_CONFIG` (precompilano i default per questo controller:
`lcd_cmd_bits=32`, `lcd_param_bits=8`, `quad_mode=true`). Implementazione:
`tx_param()`/`tx_color()` (helper statici col framing QSPI, opcode `0x02`/
`0x32`), `panel_sh8601_init()`, `panel_sh8601_draw_bitmap()` (CASET/RASET per
frame), mirror X supportato/mirror Y no, swap_xy non supportato. Zero
riferimenti a touch/I2C (driver puramente QSPI). SPDX Espressif,
Apache-2.0 — trattalo come libreria vendor upstream.

---

### `WSOLED_Touch` — `WSOLED_Touch.h` (+ `touch_bsp.c`, `touch_lvgl_indev.c`)

**Ruolo**: driver touch FT3168 su I2C, con wiring LVGL opzionale separato.
Piccolo, scritto ad-hoc (non un componente Espressif ufficiale).

**Header pubblico (`WSOLED_Touch.h`)**, umbrella su `touch_bsp.h`:
- `Touch_Init(void)` — porta su il bus (`Core_I2CBusInit()`, libreria
  `WSOLED_Core`) e configura il touch FT3168. Nessuna dipendenza da LVGL.
- `getTouch(uint16_t *x, uint16_t *y)` → `1` se c'è un tocco, `0` altrimenti.
- `Touch_RegisterLvglIndev(void)` — **opzionale**, da chiamare dopo
  `Display_Init()`: registra il touch come input device LVGL (pointer).

**`touch_bsp.c`** (hardware, LVGL-agnostico):
- FT3168 indirizzo `0x38`, letture via `Core_I2CBusInit()` per il bus (non
  installa più il driver I2C direttamente: prima del refactor era l'unico
  posto nel repo che lo faceva, ora quel ruolo è di `WSOLED_Core`).
- `Touch_Init()`: chiama `Core_I2CBusInit()`, poi scrive `0x00` al registro
  `0x00` (modalità normale).
- `getTouch()`: legge registro `0x02` (numero di tocchi); se >0 legge 4 byte dal
  registro `0x03` — **Y nei primi 2 byte, X nei successivi 2** (ordine invertito
  rispetto a quanto ci si aspetterebbe), clampa a 536×240, poi fa
  `*y = 240 - *y` (flip verticale — necessario perché il controller conta Y al
  contrario rispetto al sistema di coordinate del pannello/LVGL).
- `I2C_writr_buff()` / `I2C_read_buff()`: helper I2C generici usati
  internamente (nome `writr` con refuso, non `write` — lasciato così, è
  cosmetico). La funzione morta `I2C_master_write_read_device()` (mai
  dichiarata in header, mai chiamata) presente nel vecchio `touch_bsp.c` è
  stata rimossa durante lo spostamento in libreria.

**`touch_lvgl_indev.c`** (LVGL glue, nuovo in questo refactor):
- `Touch_RegisterLvglIndev()`: crea e registra un `lv_indev_drv_t` di tipo
  `LV_INDEV_TYPE_POINTER` con `read_cb` che chiama `getTouch()`. Avvolge
  `lv_indev_drv_register()` in `lvgl_lock(-1)`/`lvgl_unlock()` (da
  `WSOLED_Display.h`) perché, a differenza del vecchio `lvgl_port_init()`
  monolitico (dove la registrazione avveniva prima che il task di rendering
  esistesse), ora può essere chiamata dopo che `Display_Init()` ha già
  avviato `lvgl_task` — senza il lock ci sarebbe una race tra
  `lv_indev_drv_register()` e `lv_timer_handler()` che scorre le stesse liste
  interne di LVGL.
- Questo è l'unico punto di accoppiamento tra `WSOLED_Touch` e
  `WSOLED_Display` (dipende da `lvgl_lock`/`lvgl_unlock` e da `lvgl.h`):
  `touch_bsp.c` da solo resta completamente LVGL-agnostico.

**Da sapere**: nessun uso dell'interrupt touch (GPIO41, vedi Guide.md) — questo
driver fa polling puro tramite il `read_cb` registrato in LVGL. `TP_RST` non
è gestito qui perché è fisicamente legato al reset del pannello (vedi Guide.md §7).

---

### `WSOLED_IMU` — `WSOLED_IMU.h` / `.cpp`

**Ruolo**: mini-driver per l'IMU onboard QMI8658 (solo accelerometro, non
gyro), usato oggi solo da `examples/Orientation_IMU/` ma disponibile a
qualunque sketch includa la libreria.

**Header**: `imu_init(void)` → `false` se il chip non risponde;
`imu_read_accel(float *ax, float *ay, float *az)` → valori in g.

**Implementazione**:
- Bus I2C **condiviso** col touch — `imu_init()` chiama `Core_I2CBusInit()`
  come primo passo (libreria `WSOLED_Core`), quindi funziona indipendentemente
  dal fatto che `Touch_Init()` sia stato chiamato prima, dopo o per niente
  (prima del refactor assumeva implicitamente che `Touch_Init()` avesse già
  installato il bus: un IMU-senza-Touch avrebbe fallito silenziosamente,
  indistinguibile da "chip non trovato" — bug risolto con questo spostamento).
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
dal `loop()` dello sketch che la usa. Nessun task dedicato: per design, resta
un modulo passivo (vedi `CLAUDE.md`, sezione architettura).

---

### `library.properties` (tutte e quattro le librerie)

Nessun campo `depends`: la risoluzione delle dipendenze che conta con
`--libraries`/le junction locali è quella basata sugli `#include` letterali
nel codice, scansionati ricorsivamente da arduino-cli — `depends` è
consultato solo da Library Manager/`arduino-cli lib install` per librerie
da registry, quindi sarebbe comunque inerte qui. `category`/`architectures=esp32`
seguono le convenzioni Arduino standard.

---

## `WSOLED/` — il template

### `WSOLED.ino`

**Ruolo**: sketch principale, punto di ingresso Arduino (`setup()`/`loop()`). È
l'unico file pensato per essere riscritto ad ogni nuovo progetto copiato dal
template.

- `setup()`: chiama `Display_Init()`, poi `Touch_Init()`/
  `Touch_RegisterLvglIndev()` (commenta queste due righe per un progetto senza
  touch), poi `ui_init()` sotto `lvgl_lock()` (crea gli oggetti LVGL), poi
  lascia spazio per WiFi/SD/sensori (commentati come placeholder:
  `#include <WiFi.h>` / `<SD.h>`).
- `loop()`: esempio di pattern periodico (`millis()` ogni 1000ms) che legge un
  sensore fittizio (`read_sensor()`, ritorna sempre `23.5f` — da sostituire) e
  aggiornerebbe una label LVGL sotto lock (riga commentata,
  `lv_label_set_text(ui_LabelTemp, buf)`, perché lo stub non ha ancora quell'oggetto).
  Chiude con `delay(5)`.
- Commento in testa ribadisce la regola del lock e le impostazioni Tools richieste
  (Board, Flash, Partizione, PSRAM, USB CDC).

**Dipendenze**: `WSOLED_Display.h`, `WSOLED_Touch.h` (libreria), `ui.h`.
**Da sapere**: il nome del file **deve** coincidere col nome della cartella
(vincolo Arduino) — è il primo blocco quando si copia il template. Copiando
`WSOLED/` fuori da questo repo, serve portare/collegare anche `libraries/`
(vedi `CLAUDE.md`, "Avviare un nuovo progetto").

---

### `lv_conf.h`

**Ruolo**: configurazione LVGL **a livello di progetto** (letta grazie a
`LV_CONF_INCLUDE_SIMPLE` in `build_opt.h`). È sostanzialmente il template
default di LVGL 8.3.11 con pochi valori adattati a questa board — **non è stata
sfoltita** per risparmiare RAM/flash: quasi tutti i widget ed extra sono
abilitati (`=1`). Resta deliberatamente per-sketch (non spostata in libreria):
ogni progetto deve poter tunare la propria config LVGL indipendentemente
(es. abilitare `LV_USE_FS_FATFS` per caricare immagini da SD in un futuro
esempio, senza toccare gli altri).

Valori significativi/non-default:
- `LV_COLOR_DEPTH 16` + `LV_COLOR_16_SWAP 1` — RGB565 con byte-swap, richiesto
  dall'interfaccia QSPI di questo pannello.
- `LV_TICK_CUSTOM 0` — il tick NON è automatico: coerente con
  `lvgl_tick_cb()`/`lv_tick_inc()` chiamato a mano ogni 2ms in
  `WSOLED_Display.cpp`.
- `LV_MEM_SIZE (48*1024)` — 48KB di heap interno LVGL (allocazione statica, RAM
  interna, non PSRAM). Separato dai buffer di disegno DMA allocati a parte in
  `WSOLED_Display.cpp`.
- `LV_DISP_DEF_REFR_PERIOD` / `LV_INDEV_DEF_READ_PERIOD` = 30ms — periodo interno
  di LVGL per invalidazione/lettura input; il task di rendering vero e proprio
  (`lvgl_task` in `WSOLED_Display.cpp`) ha la sua logica di delay indipendente.
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
`esp_lcd_new_panel_sh8601()` in `WSOLED_Display.cpp`, e con le impostazioni del
progetto SquareLine (deve restare 16 bit).

---

### `build_opt.h`

**Ruolo**: singola riga di flag globali passati dall'IDE Arduino al compilatore
per **tutti** i file dello sketch (inclusi quelli delle librerie in `libraries/`
pullate dentro la build):

```
-DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE
```

- `LV_CONF_INCLUDE_SIMPLE`: LVGL cerca `lv_conf.h` lungo l'include path (quello di
  progetto) invece che a percorso relativo fisso — è quello che fa funzionare il
  `lv_conf.h` di questa cartella.
- `LV_LVGL_H_INCLUDE_SIMPLE`: i file generati da SquareLine includono `lvgl.h` in
  modo "semplice" (`#include "lvgl.h"` invece di percorso relativo) — senza
  questo, dopo un export SquareLine si ottengono errori `lvgl.h: No such file`.

Resta per-sketch come `lv_conf.h`, per lo stesso motivo (ogni progetto deve
poter avere le proprie flag di build).

**Da sapere**: riconosciuto solo da Arduino IDE (meccanismo `build_opt.h`
specifico dei sketch), non da `arduino-cli` in automatico a meno che il file sia
nella cartella dello sketch — è lì, quindi `arduino-cli compile` lo applica
correttamente (verificato: compilazione riuscita con le librerie in
`libraries/`, vedi `CLAUDE.md`).

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

Include le stesse quattro librerie di `WSOLED/` (`WSOLED_Display`,
`WSOLED_Touch`, `WSOLED_Core`, più `WSOLED_IMU` per l'accelerometro) — nessuna
copia locale del codice hardware. Ha solo i propri `build_opt.h`, `lv_conf.h`
(per-sketch per design, vedi sopra) e il file specifico della demo:

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
- `loop()`: legge l'IMU ogni 20ms (media di 8 campioni per ridurre il rumore),
  filtro esponenziale lento (costante 0.05) su pitch/roll filtrati, aggiorna la
  UI ogni 300ms sotto lock. Calibrazione (`s_calibrate_req`, impostato dal
  bottone) azzera l'offset corrente.
- Roll/pitch calcolati da sola accelerazione (`atan2f`) — **nessuno yaw**
  possibile con solo accelerometro; se gli assi risultano invertiti sul tuo
  mezzo, i punti da modificare sono commentati esplicitamente nel `loop()`.
- `setup()`: `Display_Init()` → `Touch_Init()`/`Touch_RegisterLvglIndev()` →
  `leveling_ui_create()` sotto lock → `imu_init()`.

**Dipendenze**: `lvgl.h`, `WSOLED_Display.h`, `WSOLED_Touch.h`, `WSOLED_IMU.h`.

---

## File a livello repository

### `README.md`

Introduzione e istruzioni d'uso del template in italiano: tabella file, setup
Arduino IDE (incluso il collegamento delle librerie condivise), spiegazione
`build_opt.h`, procedura "avvia un nuovo progetto", dove scrivere la logica
applicativa, note hardware (SD condivisa, I2C condiviso, GPIO liberi),
descrizione dell'esempio incluso. È il documento rivolto a un umano che apre
il repo per la prima volta.

### `CLAUDE.md`

Guida operativa per lavorare sul repo con Claude Code: comandi di build/verifica
(incluso `arduino-cli` con `--libraries`), architettura delle librerie
condivise in `libraries/`, workflow SquareLine, vincoli hardware, convenzioni
(dove scrivere la logica, gestione del lock). Non ripete il dettaglio
file-per-file (quello è qui, in `FILES.md`) né il pinout completo (quello è in
`ESP32-S3-AMOLED-1.91-Guide.md`).

### `ESP32-S3-AMOLED-1.91-Guide.md`

Guida di riferimento hardware/board-level (non file-per-file): identità scheda,
pinout completo, pin da non toccare, toolchain, flashing/boot mode, dettagli
display/touch/IMU/SD/batteria/WiFi, checklist di errori comuni. Cross-verificata
col codice di questo repo (vedi commit `b8a7fd8`): gli snippet ora riflettono i
valori confermati funzionanti (framing QSPI, MADCTL, WRCTRLD, flip Y touch), con
note esplicite dove non è stato possibile verificare con certezza (revisione SD
V1/V2, valore CTRL1 IMU). I riferimenti a `touch_bsp.c`/`esp_lcd_sh8601` in
questa guida citano l'esempio vendor Waveshare esterno usato come riferimento
quando la guida è stata scritta, non i file di questo repo (spostati in
`libraries/` da questo refactor) — non necessita aggiornamenti per questo.

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
