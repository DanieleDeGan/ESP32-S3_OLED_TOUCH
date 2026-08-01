# FILES.md — Dettaglio file per file

Reference completa di ogni file sorgente del repo: scopo, contenuto, dipendenze,
cosa NON toccare. Per il pinout/hardware della board vedi
`ESP32-S3-AMOLED-1.91-Guide.md`; per l'architettura d'insieme e i comandi di build
vedi `CLAUDE.md`. Qui il livello è quello del singolo file.

Il boilerplate hardware (display, touch, IMU, microSD, bus I2C) e di
comunicazione (ESP-NOW) vive in **sei** librerie Arduino condivise sotto
`libraries/` — una per periferica/funzione. Ogni sketch include solo quelle
che usa (`WSOLED/` e `examples/Orientation_IMU/` non toccano SD né ESP-NOW;
`examples/Link_Node_Demo/` include solo `WSOLED_Link` e non ha nemmeno LVGL),
senza copie duplicate del codice: un bug fix in una libreria vale per tutti
gli sketch che la usano. Restano invece deliberatamente per-sketch (non
condivisi) `lv_conf.h` e `build_opt.h`, perché sono configurazione di
progetto, non codice — vedi le rispettive sezioni sotto `WSOLED/`.

| Libreria | Usata da |
|---|---|
| `WSOLED_Core` | tirata dentro da `WSOLED_Touch`/`WSOLED_IMU`, mai inclusa direttamente da uno sketch |
| `WSOLED_Display` | `WSOLED/`, `Orientation_IMU`, `DHT11_SD_Logger`, `Link_Hub_Demo` |
| `WSOLED_Touch` | `WSOLED/`, `Orientation_IMU`, `Link_Hub_Demo` |
| `WSOLED_IMU` | `Orientation_IMU` |
| `WSOLED_SD` | `DHT11_SD_Logger` |
| `WSOLED_Link` | `Link_Hub_Demo`, `Link_Node_Demo` |

Non usano nessuna di queste librerie: `examples/Diag_Hub/` e
`examples/Diag_Node/` (diagnostica ESP-NOW su `esp_now.h` grezzo) e il template
**`WSOLED_C3/`**, che è per un'altra scheda — le `libraries/` sono boilerplate
della board AMOLED, non del repo in generale. Vedi le rispettive sezioni.

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

### `WSOLED_SD` — `WSOLED_SD.h` / `.cpp`

**Ruolo**: microSD onboard in **SDMMC a 1 bit**, con una API volutamente
minuscola e orientata a un solo caso d'uso: accodare righe di testo (log CSV).
Sottile strato sopra `SD_MMC` del core Arduino, non su `esp_vfs_fat_*` grezzo.

**API pubblica**:
- `SDCard_Init(void)` → `bool` — **è l'unica init del repo che può fallire per
  cause esterne** (card assente, non FAT32, scheda V1), quindi ritorna un esito
  invece di essere `void` come le altre. Idempotente e ri-tentabile: se già
  montata ritorna subito `true`, e se il mount fallisce lascia tutto pulito, così
  lo sketch può richiamarla più tardi (card infilata a board accesa).
- `SDCard_IsMounted(void)`, `SDCard_LastError(void)` (stringa breve in italiano,
  pensata per finire direttamente a schermo), `SDCard_SizeMB(void)`,
  `SDCard_Exists(const char *path)`.
- `SDCard_AppendLine(const char *path, const char *line)` — accoda una riga e il
  newline, creando il file se manca.
- `SDCard_WriteHeaderIfNew(const char *path, const char *header)` — scrive
  l'intestazione **solo se il file non esiste**, per non ripeterla ad ogni
  riavvio; ritorna `true` anche quando non c'era niente da fare.

**Implementazione**:
- Pin V2 fissi (`SD_PIN_CLK 9`, `SD_PIN_CMD 42`, `SD_PIN_D0 8`), non
  configurabili di proposito: sono saldati sulla scheda, non una scelta di
  progetto.
- `SD_MMC.begin("/sdcard", true, false, SDMMC_FREQ_DEFAULT)` —
  `mode1bit=true`; `format_if_mount_failed=**false**` (formattare la card di
  qualcuno perché "il mount non riesce" non è un comportamento accettabile);
  20MHz invece del massimo, perché in 1 bit la frequenza alta è la prima causa
  di card che montano "a volte".
- `SDCard_AppendLine()` apre, scrive e **richiude** il file ad ogni riga. Costa
  qualche millisecondo in più di un handle tenuto aperto, ma se salta
  l'alimentazione — su un camper succede — si perde al massimo l'ultima riga
  invece dell'intero file. Verificato sul campo: in una run di 66 h di
  `DHT11_SD_Logger` la card è stata estratta senza spegnere pulito e il CSV era
  integro fino all'ultimo byte, 3966 righe su 3966.

**Da sapere**:
- **Nessun `lvgl_lock()` attorno a queste chiamate**: la SDMMC è un controller a
  sé e non condivide pin né bus col QSPI del pannello (unico caso, in questo
  repo, di periferica che convive col rendering senza arbitraggio). Sono però
  bloccanti per decine di ms: da `loop()`/da un task tuo, **mai** da dentro una
  callback di evento LVGL.
- **Solo schede V2.** Sulle V1 la SD sta su SPI3 con CLK=GPIO47, lo stesso pin
  del PCLK del display: non basta cambiare i `#define`, servirebbe condividere
  l'host SPI2 riscrivendo anche `WSOLED_Display`. Non c'è modo di distinguere le
  revisioni a runtime (l'esempio ufficiale Waveshare le seleziona a compile-time
  con `#ifdef VersionControl_V2`) — se `SDCard_Init()` fallisce con una card
  sicuramente buona e FAT32, la scheda è probabilmente una V1.
- Su V2 il GPIO9 porta **anche il TE (tearing effect) del pannello**. Resta
  inattivo finché non gli si manda il comando `0x35`, che la sequenza di init in
  `WSOLED_Display.cpp` non manda: non abilitarlo, o va in conflitto col clock
  della card.
- Nessuna API di lettura/enumerazione: si può solo scrivere e chiedere se un
  path esiste. Conseguenza pratica in `WSOLED_SD.cpp:92`:
  `SDCard_WriteHeaderIfNew()` controlla l'**esistenza** del file, non che
  l'intestazione combaci — cambiando il formato di un CSV già presente su una
  card, le righe nuove si accodano in silenzio sotto un header vecchio (vedi la
  nota in testa a `DHT11_SD_Logger.ino`).

---

### `WSOLED_Link` — `WSOLED_Link.h` (+ `link_peer.h/.cpp`, `link_node.cpp`, `link_hub.cpp`)

**Ruolo**: livello di comunicazione **ESP-NOW** hub↔nodi per il sistema camper.
Scelto invece di MQTT/WiFi perché alcuni nodi sono a batteria e non serve
infrastruttura broker/AP. **Indipendente da LVGL e da `WSOLED_Display`**: gira
anche su una scheda senza schermo, che è il caso tipico di un nodo sensore vero
(vedi `examples/Link_Node_Demo`). Costruita sopra la libreria ufficiale
`ESP_NOW`/`ESP_NOW_Peer` bundled nel core Arduino ESP32
(`.../packages/esp32/hardware/esp32/<versione>/libraries/ESP_NOW/`), non su
`esp_now.h` grezzo: quella gestisce già peer, canale e scoperta di mittenti
sconosciuti.

**`WSOLED_Link.h`** — unico header pubblico:
- `WSOLED_LINK_CHANNEL` (6) — canale WiFi fisso, **deve** essere lo stesso su
  hub e nodi. Se un giorno l'hub farà anche da AP per un webserver, va
  coordinato con il canale dell'AP.
- `link_node_type_t` (UNKNOWN/HUB/SENSOR_TEMPERATURE/SENSOR_WATER_LEVEL/
  SENSOR_BATTERY/ACTUATOR) e `link_msg_type_t` (HELLO/WELCOME/DATA/COMMAND).
  Aggiungere tipi **in coda** non rompe la compatibilità: sul wire è un `uint8_t`.
- `link_message_t` — payload unico da **37 byte**: `protocol_version`,
  `msg_type`, `node_type` (del mittente), `name[16]`, `seq`, `battery_mv`
  (0 = alimentazione fissa), `value[3]` (significato dipendente dal tipo).
  Molto sotto i 250 byte di `ESP_NOW_MAX_DATA_LEN` v1.0 — limite scelto
  deliberatamente al posto dei 1470 della v2.0 per restare compatibili con
  qualunque chip ESP32 finisca a fare da nodo.
- `Link_Init(self_type, self_name)` (una sola volta in `setup()`; decide il
  ruolo), `Link_OnMessage(cb)`.
- Ruolo nodo: `Link_Node_Poll()`, `Link_Node_IsPaired()`, `Link_Node_SendData()`.
- Ruolo hub: `Link_Hub_Poll()`, `Link_Hub_SetPairingMode()`,
  `Link_Hub_GetPeerCount()`, `Link_Hub_GetPeerInfo()`, `Link_Hub_SendCommand()`.
- Chiamare le `Link_Node_*` dopo essersi inizializzati come hub (o viceversa)
  non fa nulla: il ruolo è deciso una volta sola da `Link_Init()`.

**`link_peer.cpp`** — parte comune ai due ruoli:
- `Link_Init()`: `WIFI_STA` con attesa di `WiFi.STA.started()` **a timeout 5 s**
  (evita l'hang infinito se il driver non parte) → `ESP_NOW.begin()` → e solo
  **dopo** `esp_wifi_set_channel()`. L'ordine non è cosmetico: un
  `WiFi.setChannel()` chiamato prima viene ignorato in silenzio su alcune
  combinazioni di chip, e il frame esce sul canale sbagliato — l'invio locale
  sembra riuscito ma `onSent()` è sempre `false`. Poi `esp_wifi_set_protocol()`
  con lo stesso bitmask 11B|11G|11N su entrambi i lati (i default variano tra
  generazioni di chip) e `esp_wifi_set_ps(WIFI_PS_NONE)` (il modem-sleep fa
  perdere unicast mentre il broadcast passa lo stesso).
- `link_parse_message()`: valida lunghezza esatta e `protocol_version`, e copia
  **sempre con `memcpy`** in una struct locale — mai un cast diretto del buffer
  ricevuto, che arriva senza garanzie di allineamento (da cui anche il
  `__attribute__((packed))` sulla struct, necessario e non solo prudente).
- `class LinkPeer : public ESP_NOW_Peer` — un peer (il nodo visto dall'hub, o
  l'hub visto dal nodo). `onReceive()` è generico rispetto al ruolo: valida,
  aggiorna `lastSeenMs`/`lastData`, e se riceve un HELLO da un peer **già noto**
  rialza `welcomePending` (un nodo che si riavvia perde il pairing mentre l'hub
  non lo dimentica mai — senza questo resterebbe in attesa per sempre, perché
  `onNewPeer` non riscatta per un MAC già registrato).
- `sendReliable()`: invia, attende la conferma di `onSent()` e ritenta, con
  backoff crescente + **jitter casuale** (`30 + attempt*40 + random(0,60)` ms) —
  un ritardo fisso rifarebbe collidere due nodi che hanno fallito nello stesso
  istante.
- **La sincronizzazione tra `onSent()` (task del driver WiFi, tipicamente Core 0)
  e chi attende la conferma (`loop()`, tipicamente Core 1) usa un semaforo
  FreeRTOS, non un `volatile bool`**: `volatile` impedisce solo il riordino del
  compilatore, non garantisce la visibilità tra core su un dual-core. Con un bool
  nudo il ritentativo non vedeva mai la conferma in tempo ed esauriva sempre
  tutti i tentativi anche a invio riuscito — bug reale trovato su hardware.

**`link_node.cpp`** — ruolo nodo: HELLO in broadcast ogni
`LINK_HELLO_INTERVAL_MS` (2000) finché non associato, poi DATA in unicast con
`sendReliable()`. `node_on_new_peer()` accetta il WELCOME dell'hub (scarta
qualunque altro `msg_type`/`node_type`) e si ferma al primo: non ci si
"ri-accoppia" con un secondo hub.

**`link_hub.cpp`** — ruolo hub:
- `hub_on_new_peer()` accetta un HELLO da MAC sconosciuto **solo** se è
  broadcast **e** `Link_Hub_SetPairingMode(true)` è attivo.
- Il WELCOME **non parte mai da dentro il callback di ricezione** (gira nel task
  del driver WiFi e va tenuto breve, stessa regola dei callback LVGL): viene
  accodato con `welcomePending` e inviato da `Link_Hub_Poll()`, dal `loop()`. Il
  flag viene pulito **solo a invio riuscito**, così un fallimento transitorio si
  ritenta al giro dopo invece di bruciare la finestra di pairing di quel nodo.
- Registro peer in array statico da `ESP_NOW_MAX_TOTAL_PEER_NUM`, **solo in RAM**
  (nessuna persistenza SD/NVS: scelta deliberata, non ancora implementata — ad
  ogni riavvio dell'hub i nodi vanno riassociati).
- Scritto dal callback di ricezione e letto da `loop()`/task LVGL: concorrenza
  vera, protetta da `portMUX_TYPE` — a differenza di `Core_I2CBusInit()`, che è
  chiamata solo in sequenza da `setup()` e non ha bisogno di lock.

**Da sapere**:
- **Scoperta bidirezionale**: `ESP_NOW.onNewPeer()` scatta per MAC *sorgente*
  sconosciuto, quindi non è una cosa del solo hub — anche il nodo deve gestirlo,
  perché l'hub gli è sconosciuto finché non arriva il WELCOME. `Link_Init()`
  registra il gestore giusto in base al ruolo.
- **Niente RSSI nella callback applicativa**: la libreria ESP_NOW ufficiale lo
  espone solo in `onNewPeer()`, non nel dispatch `onReceive()` dei peer già
  aggiunti. Fornirlo sarebbe disponibile per il solo primissimo messaggio di
  pairing e non per i DATA successivi — incoerenza evitata di proposito.
- **Limite noto sull'hardware**: l'unicast tra hub ESP32-S3 e nodo ESP32
  "classico" (Xtensa D0WD) è risultato inaffidabile/lento ad associarsi
  (broadcast sempre ok, WELCOME/unicast spesso perso), coerente con
  [espressif/arduino-esp32#10895](https://github.com/espressif/arduino-esp32/issues/10895).
  Con nodi ESP32-C3 il pairing è immediato. Per nuovi nodi preferire S2/S3/C3/C6.
- **Residui di diagnostica ancora nel codice** (marcati `DIAGNOSTICA TEMPORANEA`
  dall'autore, da rimuovere quando la campagna di test è chiusa):
  `link_peer.h:62` porta il default `ack_timeout_ms` a **1000 ms** invece dei
  300 ms documentati in `CLAUDE.md`, il che rende falsa anche la nota
  "bloccante per al massimo ~1s" in `WSOLED_Link.h:136` (il caso peggiore reale
  è ~3×(1000+50) ≈ 3,1 s, e `Link_Hub_Poll()` lo eredita per ogni WELCOME);
  `link_peer.cpp:70` stampa una riga sulla seriale **ad ogni tentativo di invio**.

---

### `library.properties` (tutte e sei le librerie)

Nessun campo `depends`: la risoluzione delle dipendenze che conta con
`--libraries`/le junction locali è quella basata sugli `#include` letterali
nel codice, scansionati ricorsivamente da arduino-cli — `depends` è
consultato solo da Library Manager/`arduino-cli lib install` per librerie
da registry, quindi sarebbe comunque inerte qui. `category`/`architectures=esp32`
seguono le convenzioni Arduino standard.

---

## `WSOLED/` — il template della board AMOLED

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

## `WSOLED_C3/` — il template ESP32-C3 + OLED + OTA

Secondo template del repo, per i **nodi** del sistema camper. **Non condivide
niente** con la board AMOLED: nessuna libreria di `libraries/`, nessun LVGL,
nessun `lv_conf.h`/`build_opt.h`. Usa Adafruit SSD1306+GFX e i moduli del core
(`WiFi`, `ESPmDNS`, `ArduinoOTA`, `WebServer`, `Update`, `Wire`). È self-contained
per scelta: si può spostare fuori dal repo e continua a compilare.

FQBN diverso dal resto del repo:
`esp32:esp32:esp32c3:CDCOnBoot=cdc,PartitionScheme=min_spiffs`, **senza**
`--libraries libraries`.

### `WSOLED_C3.ino`

**Ruolo**: `setup`/`loop` + disegno OLED. È il file in cui va la logica
applicativa del nodo.

- Pin in cima come `static constexpr`: `PIN_SDA`=5, `PIN_SCL`=6 (scelti per **non**
  toccare `PIN_LED`=8, il LED blu onboard attivo LOW, né il tasto BOOT su GPIO9),
  `OLED_ADDR`=`0x3C`, `OLED_RST`=`-1` (moduli a 4 pin, nessuna linea di reset).
- `Wire.begin(PIN_SDA, PIN_SCL)` con `periphBegin=false` nella init dell'OLED,
  per non farsi sovrascrivere i pin appena passati.
- `drawStatus()`: schermata a riposo (hostname, `FW_VERSION`, stato WiFi, IP,
  indirizzo `/update`) ridisegnata ~4 fps dal `loop()`, più una pallina che
  rimbalza nella fascia bassa — serve a vedere a colpo d'occhio che il firmware
  gira e non è piantato.
- `onOtaProgress()`: registrata con `net_setOtaProgressCb()` **prima** di
  `net_begin()`, disegna la barra di avanzamento durante un update. `otaActive`
  sospende il disegno normale mentre l'update è in corso.

### `net_ota.h` / `net_ota.cpp`

**Ruolo**: tutto il boilerplate di rete e OTA, isolato dal `.ino`. Di norma non
si tocca.

**API pubblica** (`net_ota.h`, 31 righe):
- `net_setOtaProgressCb(cb)` — callback opzionale `(int percent, const char *what)`
  per il feedback a schermo. `percent` è 0..100, oppure **-1 se la dimensione è
  ignota** (upload web senza `Content-Length`). Va impostata **prima** di
  `net_begin()`.
- `net_begin()` — una volta in `setup()`, dopo `Serial` e dopo l'init del display
  se vuoi vedere l'avanzamento: connette il WiFi (bloccante, timeout 15 s, poi
  ritenta in background), avvia `ArduinoOTA` (che porta su anche mDNS con
  `OTA_HOSTNAME`) e il web server con `/` (form) e `/update`
  (POST multipart → `Update`).
- `net_loop()` — **a ogni giro** di `loop()`: `ArduinoOTA.handle()` +
  `server.handleClient()`. Se il `loop()` blocca a lungo, l'OTA smette di
  rispondere: è il vincolo principale da rispettare aggiungendo logica.
- `net_isConnected()`, `net_ip()` — stato per la UI.

**Da sapere**: l'autenticazione (password ArduinoOTA + basic-auth su `/update`)
è dimensionata per una **LAN fidata**, non per esporre la scheda su Internet.

### `secrets.h.example` → `secrets.h`

**Ruolo**: credenziali WiFi (`WIFI_SSID`/`WIFI_PASSWORD`), hostname mDNS
(`OTA_HOSTNAME`) e password OTA (`OTA_PASSWORD`, `WEB_OTA_USER`/`WEB_OTA_PASS`).

Versionato c'è **solo il `.example`** con i segnaposto: `secrets.h` è escluso dal
`.gitignore` di radice perché questo repository è **pubblico**. Si copia il
template e si riempie.

**Da sapere**: la regola nel `.gitignore` è il percorso letterale
`WSOLED_C3/secrets.h`. Copiando il template in un'altra cartella (`MioNodo/`,
o fuori dal repo) **quella regola non copre più il nuovo percorso** e va
riscritta. Se credenziali vere finiscono committate su un repo pubblico, la
risposta giusta è **cambiare la password della rete**, non riscrivere la storia
di git: una volta pubblicata va considerata compromessa.

---

## `examples/` — sketch di esempio

Sei cartelle sketch indipendenti dal template: non si copiano per iniziare un
progetto (per quello c'è `WSOLED/`), si compilano e caricano così come sono.
Nessuna contiene copie locali del codice hardware: quelle che ne hanno bisogno
includono le librerie di `libraries/` come qualunque altro sketch, le due
diagnostiche non ne usano nessuna. Quelle con schermo hanno i propri
`build_opt.h`/`lv_conf.h` (per-sketch per design, vedi sopra); quelle senza LVGL
non ne hanno bisogno e infatti non li hanno.

| Sketch | Schermo | Librerie del repo | Gira su |
|---|---|---|---|
| `Orientation_IMU` | sì | Display, Touch, IMU (+Core) | solo questa board |
| `DHT11_SD_Logger` | sì | Display, SD | solo questa board |
| `Link_Hub_Demo` | sì | Display, Touch, Link (+Core) | solo questa board |
| `Link_Node_Demo` | no | Link | qualunque ESP32 |
| `Diag_Hub` | no | nessuna | qualunque ESP32 |
| `Diag_Node` | no | nessuna | qualunque ESP32 (pensato per C3) |

---

### `Orientation_IMU/Orientation_IMU.ino`

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

### `DHT11_SD_Logger/DHT11_SD_Logger.ino`

**Ruolo**: primo sketch del repo che scrive su microSD — temperatura/umidità da
un DHT11 a schermo (tre "card": temperatura, umidità, numero di campioni) e una
riga di CSV per lettura valida. **Nessun touch**: è un logger, non si tocca.

- **Cablaggio**: modulo DHT11 a 3 pin su `DHT_DATA_PIN` (GPIO2 di default). I
  moduli a 3 pin hanno già il pull-up da 10k a bordo; con un sensore nudo a 4
  pin va aggiunto (4.7k–10k verso 3V3). **Non usare GPIO3** (strapping JTAG,
  deve restare flottante al reset e il modulo tiene DATA in pull-up) né 26 e
  33–37 (PSRAM). Sulla versione senza header a pettine (SKU 28596) i GPIO liberi
  sono piazzole da saldare: scegli il pin in base a quale riesci a raggiungere.
- **Cadenza**: `SAMPLE_PERIOD_MS` (60000). Il valore compare **solo** lì:
  l'etichetta a schermo e la riga di `setup()` sulla seriale sono generate da
  quel define, non scritte a mano.
- **CSV** `/dht11_log.csv`, colonne
  `boot_id,n,secondi_da_accensione,temperatura_C,umidita_pct`. La scheda non ha
  un RTC tamponato, quindi l'unico riferimento temporale onesto è "secondi da
  accensione" — che però riparte da zero ad ogni avvio, insieme a `n`, in un file
  che invece sopravvive ai riavvii. Da qui `boot_id`: un contatore in **NVS**
  (`Preferences`, namespace `dht11log`) incrementato da `boot_id_next()` ad ogni
  accensione e condiviso da tutte le righe di quella run. È mostrato anche nel
  titolo a schermo (`(avvio #N)`), così si sa quali righe del file sta scrivendo
  la scheda che si ha davanti. Ritorna 0 se la NVS non si apre: sentinella
  riconoscibile, non un conteggio sballato.
- **Senza microSD funziona lo stesso**: valori a schermo, riga di stato in rosso
  con `SDCard_LastError()`, e ritenta il mount ogni `SD_RETRY_PERIOD_MS` (30 s)
  così la card si può infilare a scheda accesa.
- `take_sample()` non gira sotto `lvgl_lock`: la lettura del DHT11 tiene le
  interruzioni disabilitate (il protocollo si misura al microsecondo) e la
  scrittura SD blocca per decine di ms — bloccare anche il rendering per tutto
  quel tempo non servirebbe a niente. Il lock si prende **solo alla fine**,
  dentro `ui_refresh()`.
- `loop()`: la finestra di campionamento riparte da `millis()` **alla fine** di
  ogni campione, così la barra di avanzamento riflette la cadenza reale invece di
  accumulare ritardo se una scrittura è andata lunga. Il prezzo è una deriva di
  ~34 ms a campione (durata di `take_sample()` + granularità del `delay(5)`);
  se un giorno servisse una cadenza assoluta, si cambia in
  `win_start_ms += win_span_ms`.

**Dipendenze**: `DHT.h` (Adafruit "DHT sensor library" + "Adafruit Unified
Sensor", da Library Manager — le stesse dei nodi ESP32-C3 del sistema camper
fuori da questo repo, così il DHT11 si legge allo stesso modo su hub e nodi),
`Preferences.h` (bundled nel core), `lvgl.h`, `WSOLED_Display.h`, `WSOLED_SD.h`.

**Da sapere**: validato sul campo con una run di **66 h** (3966 campioni a 60 s,
zero letture fallite, zero scritture fallite, nessun buco nella sequenza). Il
formato CSV è cambiato dopo quella run (l'aggiunta di `boot_id`): su una card
che contiene ancora un log a 4 colonne, `SDCard_WriteHeaderIfNew()` non se ne
accorge — controlla solo se il file esiste — e le righe nuove si accodano a 5
campi sotto un'intestazione che ne dichiara 4. Rinomina o cancella il vecchio
file prima di riusare quella card.

---

### `Link_Hub_Demo/Link_Hub_Demo.ino`

**Ruolo**: hub del sistema camper — valida `WSOLED_Link` dal lato che ha lo
schermo. Mostra fino a `MAX_ROWS` (6) nodi associati con nome, tipo, ultimo
valore e "visto N s fa", più un bottone touch che accende/spegne la modalità
pairing. UI scritta a mano nello stesso stile di `Orientation_IMU`, non
SquareLine.

- Le 6 righe sono **pre-create nascoste** in `hub_ui_create()` e poi
  mostrate/nascoste a runtime, invece di creare e distruggere oggetti LVGL ogni
  volta che cambia il numero di nodi: niente allocazioni nel `loop()`.
- `pairing_toggle_cb()` è una callback di evento LVGL: **niente lock** (già
  preso) e corta — chiama solo `Link_Hub_SetPairingMode()` e aggiorna due
  etichette.
- `loop()`: `Link_Hub_Poll()` ad ogni giro (è lì che partono i WELCOME accodati),
  aggiornamento UI ogni 500 ms sotto `lvgl_lock`.
- Una riga mostra `--` finché `last_data.protocol_version` non combacia, cioè
  finché da quel nodo non è arrivato un vero DATA: un nodo appena associato
  compare subito, senza inventare un valore.

**Dipendenze**: `lvgl.h`, `WSOLED_Display.h`, `WSOLED_Touch.h`, `WSOLED_Link.h`.

**Da sapere**: da provare in coppia con `Link_Node_Demo` su una seconda scheda —
attiva il pairing qui, accendi il nodo, deve comparire una riga entro pochi
secondi con un valore che si aggiorna ogni ~5 s.

---

### `Link_Node_Demo/Link_Node_Demo.ino`

**Ruolo**: nodo sensore finto, **solo Serial** — 65 righe, nessuna dipendenza da
display/touch/pin della board AMOLED, gira su qualunque ESP32. È proprio il
punto: un nodo sensore vero del sistema camper non avrà uno schermo, e questo
sketch dimostra che `WSOLED_Link` non trascina dentro LVGL. Nessun `lv_conf.h`
né `build_opt.h` in cartella, per lo stesso motivo.

- Manda una temperatura finta (20.0–30.0 °C derivata da `millis()`) ogni ~5 s
  una volta associato.
- L'intervallo ha **jitter casuale** (±250 ms): con più nodi identici sullo
  stesso hub, un periodo fisso può farli convergere a trasmettere nello stesso
  istante man mano che i clock derivano, e a quel punto collidono ad ogni ciclo.
- `on_message()` logga il WELCOME (col MAC dell'hub) e gli eventuali COMMAND.

**Dipendenze**: `WSOLED_Link.h`.

---

### `Diag_Hub/` + `Diag_Node/` — diagnostica ESP-NOW

**Ruolo**: coppia di sketch **usa e getta**, scritti per misurare il tasso di
perdita reale dei pacchetti quando il pairing di `WSOLED_Link` si è rivelato
inaffidabile su certe combinazioni di chip. Deliberatamente al livello più basso
possibile: `esp_now.h` grezzo, **nessuna libreria di questo repo**, nessun
pairing, nessun peer unicast, nessun retry — solo broadcast di un contatore. Non
sono demo del sistema camper e non condividono nulla col resto del codice:
`diag_packet_t` (12 byte, `static_assert` sulla dimensione) è definita a mano e
identica nei due file, ed è tutto ciò che li lega.

- `Diag_Node` spara un pacchetto ogni `SEND_INTERVAL_MS` (500) in broadcast;
  `NODE_ID` va cambiato se se ne accendono più di uno. `boot_count` in
  `RTC_DATA_ATTR` + `esp_reset_reason()` servono a non contare un brownout come
  "pacchetti persi".
- `Diag_Hub` conta i **buchi nel numero di sequenza** (= perdita reale),
  i duplicati, RSSI last/min, e i drop di coda; riepilogo ogni 5 s. Distingue un
  nodo ripartito (salto all'indietro > 100) da un vero duplicato/riordino MAC.
- **La recv callback non stampa e non lavora**: copia il pacchetto in una coda
  FreeRTOS e basta, tutto il parsing e la stampa avvengono in `loop()`. È la
  stessa disciplina che in `WSOLED_Link` tiene i WELCOME fuori dal callback, qui
  applicata perché la seriale nel contesto radio fa perdere i pacchetti
  successivi.
- **Attenzione**: entrambi impostano `WIFI_PROTOCOL_LR` (Long Range), che è
  proprietario Espressif e **diverso** dal bitmask 11B|11G|11N di
  `WSOLED_Link`. I due mondi non si parlano: un Diag_Node non viene visto da un
  `Link_Hub_Demo` e viceversa. È voluto — servono a misurare il canale, non a
  interoperare.

**Da sapere**: i commenti di entrambi rimandano ai paragrafi (§1, §2, §4, §6,
§7, §8, §10) di un documento di analisi ESP-NOW che **non è in questo repo** —
i riferimenti restano leggibili come struttura del ragionamento (canale fisso
dopo l'init, callback corta, seq/dup, struct packed, contatori) ma il documento
va cercato altrove.

---

## File a livello repository

### `README.md`

Introduzione e istruzioni d'uso del template in italiano: tabella file, setup
Arduino IDE (incluso il collegamento delle librerie condivise), spiegazione
`build_opt.h`, procedura "avvia un nuovo progetto", dove scrivere la logica
applicativa, note hardware (SD condivisa, I2C condiviso, GPIO liberi),
descrizione di tutti e sei gli esempi. È il documento rivolto a un umano che
apre il repo per la prima volta, quindi resta a livello "cosa fa / come lo
provo": il dettaglio per file è qui, non lì.

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
sistema Windows/macOS, `.vscode/`, `.claude/settings.local.json` (permessi
locali di Claude Code per questa macchina/sessione, non da condividere) e
**`WSOLED_C3/secrets.h`** (credenziali WiFi/OTA reali — vedi la sezione
`WSOLED_C3/` sopra; il repository è pubblico).

**Da sapere**: la regola su `secrets.h` è un percorso letterale, non un pattern
`*/secrets.h`. Vale per quella cartella e basta: chi copia il template altrove
deve riscriverla.

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
