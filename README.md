# Starter LVGL + SquareLine — Waveshare ESP32-S3-Touch-AMOLED-1.91

Template riutilizzabile per sviluppare interfacce LVGL (disegnate in SquareLine
Studio) sulla Waveshare ESP32-S3-Touch-AMOLED-1.91 (AMOLED 536×240, SH8601, touch
FT3168). Tutto il boilerplate di basso livello è isolato: nel tuo sketch resta
solo la tua logica.

## Contenuto della cartella

| File | Ruolo |
|------|-------|
| `WSOLED.ino` | sketch principale: `setup`/`loop`, la tua logica (UI, WiFi, SD, sensori) |
| `lv_conf.h` | configurazione LVGL **a livello di progetto** |
| `build_opt.h` | flag di compilazione (vedi sotto) |
| `ui.h` / `ui.c` | **stub** segnaposto: vengono sostituiti dall'export di SquareLine |

Il boilerplate di basso livello (display, touch, IMU, bus I2C) non è più dentro
la cartella dello sketch: vive in quattro librerie Arduino condivise sotto
`libraries/`, alla radice del repo, cosi' ogni sketch include solo quello che
usa:

| Libreria | Ruolo |
|---|---|
| `WSOLED_Core` | bring-up del bus I2C condiviso (idempotente) |
| `WSOLED_Display` | pannello SH8601 (QSPI) + LVGL + task di rendering + mutex |
| `WSOLED_Touch` | driver touch FT3168, con wiring LVGL opzionale |
| `WSOLED_IMU` | driver IMU QMI8658 onboard |

Unica dipendenza esterna da installare tramite Library Manager: **LVGL 8.3.x**.
Le quattro librerie `WSOLED_*` sono locali a questo repo (vedi setup sotto).

## Impostazioni Arduino IDE (Tools)

- Board: **ESP32S3 Dev Module**
- Flash Size: **16MB**
- Partition Scheme: **16M Flash (3MB APP/9.9MB FATFS)**
- PSRAM: **OPI PSRAM**
- USB CDC On Boot: **Enabled**
- CPU Frequency: **240 MHz**

### Collegare le librerie condivise (una tantum)

L'IDE cerca le librerie in `Documents/Arduino/libraries/`, non dentro questo
repo. Per farci vedere le quattro librerie `WSOLED_*` senza copiarle a mano
(e senza perdere gli aggiornamenti quando le modifichi), crea una junction
per ciascuna — su Windows non serve essere amministratore né attivare la
"Developer Mode" (necessaria invece per i symlink veri):

```powershell
$repo = "<percorso di questo repo>\libraries"
$dest = "$env:USERPROFILE\Documents\Arduino\libraries"
foreach ($name in "WSOLED_Core","WSOLED_Display","WSOLED_Touch","WSOLED_IMU") {
  New-Item -ItemType Junction -Path "$dest\$name" -Target "$repo\$name"
}
```

Dopo, riavvia l'IDE: `Sketch > Include Library` deve elencare tutte e
quattro. In alternativa, per compilare da riga di comando senza junction,
usa `arduino-cli compile --libraries libraries ...` dalla radice del repo
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

1. **Copia** l'intera cartella in `MioProgetto/`.
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

- **microSD**: condivide i pin col display (CLK=GPIO47, MISO=GPIO8; MOSI=GPIO42, CS=GPIO9).
  Non è un bus indipendente: segui il demo `SD_Test` di Waveshare. Schede ≤ 64 GB, FAT32.
- **I2C**: touch e IMU QMI8658 sono sullo stesso bus (SDA=GPIO40, SCL=GPIO39;
  indirizzi 0x38 e 0x6B). `Core_I2CBusInit()` (libreria `WSOLED_Core`) lo porta
  su in modo idempotente: sia il touch sia l'IMU la richiamano internamente,
  quindi funzionano in qualunque ordine. Non aprire un bus indipendente sugli
  stessi pin.
- **GPIO liberi** per periferiche tue: 2, 3, 4, 10–16, 21, 38. Evita 26 e 33–37 (PSRAM octal).

## Esempio incluso: examples/Orientation_IMU

Sketch autosufficiente che usa l'IMU onboard QMI8658 per mostrare un indicatore
di assetto: una linea di orizzonte che si inclina con il roll e scorre con il
pitch, piu' i valori numerici. La UI e' costruita in codice (non con SquareLine).

- Apri `examples/Orientation_IMU/Orientation_IMU.ino` in Arduino IDE e compila.
- L'IMU e' sullo stesso bus I2C del touch: entrambi richiamano
  `Core_I2CBusInit()` (libreria `WSOLED_Core`, idempotente) internamente,
  quindi funzionano in qualunque ordine.
- Pitch/roll derivano dall'accelerometro (lo yaw non e' osservabile dal solo
  accelerometro). Se gli assi risultano invertiti, cambia i segni nelle due righe
  indicate nel `loop()`. Se l'IMU non risponde, prova l'indirizzo 0x6A in
  `WSOLED_IMU.cpp` (default 0x6B).
