# Starter LVGL + SquareLine — Waveshare ESP32-S3-Touch-AMOLED-1.91

Template riutilizzabile per sviluppare interfacce LVGL (disegnate in SquareLine
Studio) sulla Waveshare ESP32-S3-Touch-AMOLED-1.91 (AMOLED 536×240, SH8601, touch
FT3168). Tutto il boilerplate di basso livello è isolato: nel tuo sketch resta
solo la tua logica.

## Contenuto del repository

| Percorso | Ruolo |
|---|---|
| `WSOLED/` | **il template**: la cartella da copiare per iniziare un progetto nuovo |
| `libraries/` | le librerie condivise (display, touch, IMU, microSD, ESP-NOW) |
| `examples/` | sei sketch completi, da compilare e caricare così come sono |
| `FILES.md` | reference file-per-file: scopo, funzioni, cosa non toccare |
| `ESP32-S3-AMOLED-1.91-Guide.md` | pinout e dettagli hardware della scheda |

Dentro `WSOLED/` (e in ogni copia che ne farai):

| File | Ruolo |
|------|-------|
| `WSOLED.ino` | sketch principale: `setup`/`loop`, la tua logica (UI, WiFi, SD, sensori) |
| `lv_conf.h` | configurazione LVGL **a livello di progetto** |
| `build_opt.h` | flag di compilazione (vedi sotto) |
| `ui.h` / `ui.c` | **stub** segnaposto: vengono sostituiti dall'export di SquareLine |

Il boilerplate di basso livello non è dentro la cartella dello sketch: vive in
sei librerie Arduino condivise sotto `libraries/`, alla radice del repo, così
ogni sketch include solo quello che usa:

| Libreria | Ruolo |
|---|---|
| `WSOLED_Core` | bring-up del bus I2C condiviso (idempotente) |
| `WSOLED_Display` | pannello SH8601 (QSPI) + LVGL + task di rendering + mutex |
| `WSOLED_Touch` | driver touch FT3168, con wiring LVGL opzionale |
| `WSOLED_IMU` | driver IMU QMI8658 onboard |
| `WSOLED_SD` | microSD onboard (SDMMC 1 bit), orientata al logging testuale |
| `WSOLED_Link` | comunicazione ESP-NOW hub↔nodi, indipendente da LVGL/display |

Sono tutte locali a questo repo (vedi setup sotto), non si installano da
Library Manager. Le uniche dipendenze **esterne**:

- **LVGL 8.3.x** — serve a ogni sketch con schermo.
- **DHT sensor library** (Adafruit) + **Adafruit Unified Sensor** — solo per
  `examples/DHT11_SD_Logger/`.

## Impostazioni Arduino IDE (Tools)

- Board: **ESP32S3 Dev Module**
- Flash Size: **16MB**
- Partition Scheme: **16M Flash (3MB APP/9.9MB FATFS)**
- PSRAM: **OPI PSRAM**
- USB CDC On Boot: **Enabled**
- CPU Frequency: **240 MHz**

### Collegare le librerie condivise (una tantum)

L'IDE cerca le librerie in `Documents/Arduino/libraries/`, non dentro questo
repo. Per farci vedere le librerie `WSOLED_*` senza copiarle a mano (e senza
perdere gli aggiornamenti quando le modifichi), crea una junction per
ciascuna — su Windows non serve essere amministratore né attivare la
"Developer Mode" (necessaria invece per i symlink veri):

```powershell
$repo = "<percorso di questo repo>\libraries"
$dest = "$env:USERPROFILE\Documents\Arduino\libraries"
foreach ($name in "WSOLED_Core","WSOLED_Display","WSOLED_Touch","WSOLED_IMU","WSOLED_SD","WSOLED_Link") {
  New-Item -ItemType Junction -Path "$dest\$name" -Target "$repo\$name"
}
```

Dopo, riavvia l'IDE: `Sketch > Include Library` deve elencarle tutte e sei.
In alternativa, per compilare da riga di comando senza junction, usa
`arduino-cli compile --libraries libraries ...` dalla radice del repo
(vedi `CLAUDE.md`).

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

1. **Copia** l'intera cartella `WSOLED/` in `MioProgetto/`.
2. **Librerie condivise**: se `MioProgetto/` resta dentro questo repo (accanto a
   `WSOLED/`/`examples/`), non serve altro — condivide già `libraries/` alla
   radice. Se invece diventa un progetto/repo indipendente altrove sul disco,
   copia (o crea una junction verso) anche la cartella `libraries/` accanto ad
   esso: senza, non compila.
3. **Rinomina** lo sketch in `MioProgetto.ino` (il nome del `.ino` DEVE coincidere
   con quello della cartella, è un vincolo di Arduino).
4. **Compila e carica** così com'è: all'avvio compare "Starter pronto" centrato.
   Conferma che display, touch e LVGL funzionano (la UI è ancora lo stub).
5. In **SquareLine Studio** crea un nuovo progetto:
   - Risoluzione **536 × 240**, profondità colore **16 bit**.
   - Versione LVGL **8.3.x** (deve combaciare con `lv_conf.h` e la libreria installata).
6. **Esporta solo i file UI** ("Export UI Files") con percorso di export = la cartella
   dello sketch. I file `ui.h` / `ui.c` generati **sovrascrivono gli stub**; arrivano
   anche `ui_helpers.*`, `ui_events.*`, gli screen e gli asset.
   - **Non** usare il `.ino` né il driver TFT_eSPI generati da SquareLine: il display
     è già gestito da `WSOLED_Display`. Si tengono solo i file `ui_*`.
7. **Ricompila.** Se l'IDE non vede i file appena aggiunti, chiudilo e riaprilo (la
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

- **microSD**: dipende dalla revisione della scheda, e le due non sono
  intercambiabili.
  - **V2** (schede attuali, quello che implementa `WSOLED_SD`): **SDMMC a 1 bit**,
    CLK=GPIO9, CMD=GPIO42, D0=GPIO8. **Nessun pin in comune col bus QSPI del
    pannello**, quindi SD e LVGL convivono senza arbitraggio e senza lock.
  - **V1** (schede vecchie): SD su SPI3 con CLK=GPIO47, cioè lo stesso pin del
    PCLK del display. Lì i due sono fisicamente sullo stesso clock e servirebbe
    condividere l'host SPI2. **Non supportata** da `WSOLED_SD`: se
    `SDCard_Init()` fallisce con una card sicuramente buona e FAT32, la scheda è
    probabilmente una V1.

  Non c'è modo di distinguerle a runtime (anche l'esempio ufficiale Waveshare le
  seleziona a compile-time). Schede ≤ 64 GB, FAT32: le exFAT non montano, e la
  libreria non le formatta di sua iniziativa.
- **I2C**: touch e IMU QMI8658 sono sullo stesso bus (SDA=GPIO40, SCL=GPIO39;
  indirizzi 0x38 e 0x6B). `Core_I2CBusInit()` (libreria `WSOLED_Core`) lo porta
  su in modo idempotente: sia il touch sia l'IMU la richiamano internamente,
  quindi funzionano in qualunque ordine. Non aprire un bus indipendente sugli
  stessi pin.
- **GPIO liberi** per periferiche tue: 2, 4, 10–16, 21, 38. Evita 26 e 33–37
  (PSRAM octal). Il **GPIO3** è elettricamente libero ma è un pin di strapping
  (JTAG): deve restare flottante al reset, quindi non usarlo per niente che
  tenga la linea alta o bassa all'accensione — un modulo sensore con pull-up a
  bordo, per esempio, la tiene alta.
- Sulla versione senza header a pettine (SKU 28596) i GPIO liberi sono piazzole
  da saldare, non un connettore: scegli il pin anche in base a quale riesci a
  raggiungere fisicamente.

## Esempi inclusi

Non si copiano per iniziare un progetto — per quello c'è `WSOLED/`. Si aprono
in Arduino IDE e si caricano così come sono. Tutti costruiscono la UI in codice,
nessuno usa SquareLine.

| Sketch | Cosa fa | Gira su |
|---|---|---|
| `Orientation_IMU` | livella per camper con l'IMU onboard | questa board |
| `DHT11_SD_Logger` | temperatura/umidità a schermo + log CSV su microSD | questa board |
| `Link_Hub_Demo` | hub ESP-NOW: lista dei nodi associati + pairing da touch | questa board |
| `Link_Node_Demo` | nodo sensore finto, solo Serial | qualunque ESP32 |
| `Diag_Hub` / `Diag_Node` | diagnostica ESP-NOW grezza (misura i pacchetti persi) | qualunque ESP32 |

### `Orientation_IMU` — livella per camper

Vista dall'alto del mezzo con le 4 ruote: ogni ruota mostra di quanti cm va
rialzata per mettere il camper in piano (verde = ok, ambra = poco, rosso = molto),
più una bolla centrale e un pulsante CALIBRA che azzera la posizione corrente.

- **Imposta `TRACK_MM` e `WHEELBASE_MM`** in testa allo sketch sul tuo veicolo:
  i default sono di un Adria Matrix Axess 680 SP su Ducato Maxi. La direzione
  resta corretta comunque, ma i centimetri dipendono dalle dimensioni reali.
- Pitch/roll derivano dall'accelerometro (lo yaw non è osservabile dal solo
  accelerometro). Se sinistra/destra o avanti/dietro risultano invertiti, cambia
  i segni nelle due righe indicate nel `loop()`. Se l'IMU non risponde, prova
  l'indirizzo 0x6A in `WSOLED_IMU.cpp` (default 0x6B).

### `DHT11_SD_Logger` — sensore a schermo e log su microSD

Legge un DHT11 ogni 60 s (`SAMPLE_PERIOD_MS`), mostra temperatura, umidità e
numero di campioni, e accoda ogni lettura valida a `/dht11_log.csv` sulla card.

- **Cablaggio**: modulo a 3 pin, DATA su GPIO2 (`DHT_DATA_PIN`, cambiabile). I
  moduli a 3 pin hanno già il pull-up da 10k a bordo; con un sensore nudo a 4
  pin va aggiunto (4.7k–10k verso 3V3).
- **Senza card funziona lo stesso**: valori a schermo, avviso in rosso, e ritenta
  il mount ogni 30 s — puoi infilarla a scheda accesa.
- Colonne del CSV: `boot_id,n,secondi_da_accensione,temperatura_C,umidita_pct`.
  La scheda non ha un RTC tamponato, quindi l'unico riferimento temporale onesto
  è "secondi da accensione", che riparte da zero ad ogni avvio; `boot_id` è un
  contatore in NVS che distingue una run dall'altra dentro lo stesso file, ed è
  mostrato anche a schermo in alto a sinistra.
- Provato in continuo per 66 ore: 3966 campioni, nessuna lettura persa e file
  integro anche estraendo la card senza spegnere (ogni riga viene aperta,
  scritta e chiusa singolarmente).

### `Link_Hub_Demo` + `Link_Node_Demo` — la rete ESP-NOW del camper

Da provare in coppia, su due schede: `Link_Hub_Demo` sulla AMOLED,
`Link_Node_Demo` su qualunque altro ESP32 (non serve schermo — è il punto:
un nodo sensore vero non ne ha uno).

1. Carica l'hub sulla board AMOLED e il nodo sull'altra.
2. Sull'hub premi **ASSOCIA NUOVO NODO**: fuori da quella finestra i nodi
   sconosciuti vengono ignorati.
3. Accendi il nodo: deve comparire in una riga entro pochi secondi, con un
   valore finto che si aggiorna ogni ~5 s.

I nodi associati vivono **solo in RAM**: al riavvio dell'hub vanno riassociati.
Per nuovi nodi preferisci varianti recenti (S2/S3/C3/C6): con un ESP32 "classico"
il pairing unicast è risultato lento e inaffidabile su hardware reale.

### `Diag_Hub` / `Diag_Node` — diagnostica ESP-NOW

Coppia usa e getta, scritta per misurare quanti pacchetti si perdono davvero sul
canale. Nessuna libreria di questo repo, nessun pairing, nessun retry: il nodo
spara un contatore in broadcast ogni 500 ms e l'hub conta i buchi nella sequenza,
stampando un riepilogo ogni 5 s (ricevuti, persi, duplicati, RSSI).

Girano in modalità Long Range, quindi **non** si parlano con `WSOLED_Link`:
servono a misurare il canale, non a interoperare.
