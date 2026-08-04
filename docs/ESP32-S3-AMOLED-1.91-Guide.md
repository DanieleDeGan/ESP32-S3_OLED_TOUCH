# ESP32-S3 AMOLED 1.91" — Complete Developer Guide

A practical reference for the Waveshare **ESP32-S3-AMOLED-1.91** family: display, touch, microSD, IMU, battery, Wi-Fi/BLE, and the expansion headers.

Built from the four datasheets in this project (board schematic, ESP32-S3 datasheet v2.2, RM67162 rev 0.5, QMI8658C rev 0.9), cross-checked against Waveshare's published example code.

---

## 1. Board identity

The schematic is Waveshare's 1.91-inch AMOLED development board. Four SKUs share this PCB:

| SKU | Product | Touch | 2×20 headers |
|---|---|---|---|
| 28872 | ESP32-S3-AMOLED-1.91 | No | Not fitted |
| 28873 | ESP32-S3-AMOLED-1.91-M | No | Fitted |
| 28596 | ESP32-S3-Touch-AMOLED-1.91 | Yes | Not fitted |
| 28871 | ESP32-S3-Touch-AMOLED-1.91-M | Yes | Fitted |

**Two hardware revisions exist (V1 and V2).** They differ in how the microSD slot is wired — see §9. Everything else in this guide applies to both.

### Core specification

| Item | Value |
|---|---|
| MCU | ESP32-S3R8, dual-core Xtensa LX7, up to 240 MHz |
| RAM | 512 KB SRAM + 384 KB ROM + **8 MB octal PSRAM** (in-package) |
| Flash | 16 MB (W25Q128, U3) |
| Display | 1.91" AMOLED, 536 × 240, 16.7 M colour, QSPI |
| Display driver IC | RM67162 (drive it with the **SH8601** driver — see §6.1) |
| Touch | FT3168 capacitive, I²C, 5-point, interrupt output *(touch SKUs only)* |
| IMU | QMI8658C, 6-axis (3-axis accel + 3-axis gyro), I²C |
| Storage | microSD (TF) slot |
| Radio | 2.4 GHz Wi-Fi 802.11 b/g/n, Bluetooth 5 (LE), PCB antenna + IPEX option |
| USB | Type-C, native USB (CDC-ACM / JTAG / OTG) |
| Battery | 3.7 V LiPo via MX1.25 header, PL4054 charger, ADC sense |
| Expansion | 2 × 20-pin Raspberry Pi Pico-compatible headers |

Onboard support ICs from the schematic: **TPS65137A** (U4) generates the panel's ELVDD / ELVSS rails, **PL4054** (PL1) handles LiPo charging, **APM2307** switches between USB and battery power, and **R3 (0 Ω)** selects PCB antenna vs. the IPEX connector.

---

## 2. Master pin map

Every number below is confirmed against Waveshare's example sources.

### Display (QSPI)

| Signal | GPIO | Schematic net |
|---|---|---|
| CS | **6** | `CS` |
| SCK / PCLK | **47** | `WRX_SCL` |
| DATA0 | **18** | `RD_SDI` |
| DATA1 | **7** | `DCX_RS` |
| DATA2 | **48** | `DB0` |
| DATA3 | **5** | `DB1` |
| RESET | **17** | `RESET` (via 0 Ω R13) |
| TE (tearing effect) | **9** | `TE` |
| SDO (read-back) | **8** | `SDO` |

There is **no backlight pin** — it's an AMOLED. Brightness is a display command (§6.3).

### Touch + IMU (shared I²C bus)

| Signal | GPIO | Notes |
|---|---|---|
| SCL | **39** | Shared by FT3168 and QMI8658C |
| SDA | **40** | Shared |
| TP_INT | **41** | Touch interrupt |
| TP_RST | — | Tied to the display `RESET` net; pass `-1` in software |
| IMU_INT1 | **45** | QMI8658C interrupt 1 |
| IMU_INT2 | **46** | QMI8658C interrupt 2 |

I²C addresses: **FT3168 = 0x38**, **QMI8658C = 0x6B**.

### microSD

| Revision | Mode | Pins |
|---|---|---|
| **V2** (current) | SDMMC, 1-bit | CLK = **9**, CMD = **42**, D0 = **8** |
| **V1** (earlier) | SPI (SPI3_HOST) | CLK = **47**, MOSI = **42**, MISO = **8**, CS = **9** |

### System

| Signal | GPIO |
|---|---|
| UART0 TX | **43** |
| UART0 RX | **44** |
| USB D− | **19** |
| USB D+ | **20** |
| BOOT button | **0** |
| RESET button | CHIP_PU (not a GPIO) |
| Battery sense | **1** (ADC1_CH0) |

### Free GPIO on the expansion headers

**2, 3, 4, 10, 11, 12, 13, 14, 15, 16, 21, 38**

> Note: this repo's own `README.md`/`CLAUDE.md` list free GPIOs as 2, 3, 4, 10–16, 21 — one short of the 38 above. Not a contradiction (nothing there claims 38 is unusable), just an omission worth fixing in those docs.

---

## 3. Pins you must not touch

This is where most projects on this board break.

**GPIO 26, 33, 34, 35, 36, 37 are the octal PSRAM bus.** The "R8" in ESP32-S3R8 means 8 MB of in-package octal PSRAM, and per Table 2-14 of the ESP32-S3 datasheet, octal mode consumes SPICS1 (GPIO26) plus GPIO33–37 as DQ4–DQ7 and DQS/DM. Some of these are physically routed to the headers, which makes them look available. Driving any of them corrupts PSRAM and typically produces a boot loop or random crashes that appear unrelated. The datasheet is blunt about it: *do not use the pins connected to in-package flash/PSRAM for any other purpose.*

**GPIO 27–32** are the flash bus and are not exposed.

**Strapping pins:**
- **GPIO0** — BOOT. It has an external button; a strong external pull-down at reset forces download mode.
- **GPIO45, GPIO46** — VDD_SPI voltage and ROM message control. Both are used here by the IMU interrupts. They power up with internal pull-**downs**, so avoid adding external pull-ups.
- **GPIO3** — JTAG source select. Free to use, but keep it floating at reset.

**GPIO19 / GPIO20** are the native USB pins. Repurposing them kills USB-CDC flashing, and you'll need the BOOT button to recover.

---

## 4. Getting a toolchain running

### Arduino IDE

1. Add the ESP32 board package (Boards Manager → "esp32" by Espressif).
2. Select **ESP32S3 Dev Module**.
3. Set these, or nothing will work:

| Setting | Value |
|---|---|
| Flash Size | 16MB (128Mb) |
| PSRAM | **OPI PSRAM** |
| Partition Scheme | 16M Flash (3MB APP/9.9MB FATFS) |
| USB CDC On Boot | Enabled |
| Upload Mode | UART0 / Hardware CDC |

`PSRAM: OPI PSRAM` is the one people miss. With it wrong you get 512 KB of RAM instead of 8.5 MB, and any framebuffer allocation fails.

Libraries you'll want: `lvgl` (v8.x), and either `Arduino_GFX` or the `esp_lcd_sh8601` component shipped in Waveshare's examples.

### ESP-IDF

Target `esp32s3`, then in `menuconfig`:

- **Component config → ESP PSRAM** → enable, mode **Octal**, speed 80 MHz
- **Serial flasher config** → flash size **16 MB**
- **Partition Table** → custom, sized for 16 MB

Waveshare's repo (`waveshareteam/ESP32-S3-AMOLED-1.91`) carries ready-made examples for ADC, IMU, LVGL, SD card, Wi-Fi STA and AP, plus a factory program.

---

## 5. Flashing and boot modes

The Type-C port is wired to the ESP32-S3's **native USB**, not a USB-serial bridge. That gives you USB-CDC serial *and* a JTAG debug interface from the same connector, with no CP210x/CH340 driver to install.

Normal flashing: just plug in. The CDC-ACM controller supports host-triggered reset and download-mode entry, so `esptool` resets the chip itself.

If the board stops enumerating — usually after flashing code that reconfigures GPIO19/20, crashes early, or deep-sleeps immediately:

1. Hold **BOOT**
2. Tap **RESET**
3. Release **BOOT**

The chip comes up in ROM download mode and appears as a fresh serial device.

---

## 6. The display

### 6.1 Why "RM67162" but "SH8601" in code

The panel datasheet in this project is the Raydium RM67162, rev 0.5. That revision documents only MIPI DSI, 3-wire SPI, 4-wire SPI, 16-bit SPI, and MCU 8-bit modes selected by `IM[1:0]` — **it never describes QSPI**. QSPI on these panels is a later addition, so the register-level details aren't in the document you have.

In practice Waveshare's own examples drive this board with the **`esp_lcd_sh8601`** driver, and it works: the SH8601 and RM67162 command sets overlap for everything these panels need (sleep-out, colour mode, address window, brightness, pixel writes). Use `esp_lcd_sh8601`, or `Arduino_SH8601` if you're on Arduino_GFX. Don't go hunting for an RM67162 QSPI driver.

On the board, `IM0` and `IM1` are strapped through 0 Ω resistors (R4, R5) to fix the interface mode. Leave them alone.

### 6.2 Bring-up (ESP-IDF / Arduino with esp_lcd)

```c
#define LCD_HOST        SPI2_HOST
#define LCD_H_RES       536
#define LCD_V_RES       240
#define LCD_BPP         16

#define PIN_LCD_CS      6
#define PIN_LCD_PCLK    47
#define PIN_LCD_D0      18
#define PIN_LCD_D1      7
#define PIN_LCD_D2      48
#define PIN_LCD_D3      5
#define PIN_LCD_RST     17

static const sh8601_lcd_init_cmd_t init_cmds[] = {
    {0x11, (uint8_t[]){0x00}, 0, 120},   // sleep out, 120 ms
    {0x36, (uint8_t[]){0xF0}, 1, 0},     // MADCTL — landscape. 0x00 leaves this panel
                                          // in the wrong orientation; 0xF0 is the
                                          // confirmed-working value for 536x240 landscape.
    {0x3A, (uint8_t[]){0x55}, 1, 0},     // 16-bit / RGB565
    {0x53, (uint8_t[]){0x20}, 1, 0},     // WRCTRLD: BCTRL=1 — required, or 0x51
                                          // (brightness) below is silently ignored.
    {0x29, (uint8_t[]){0x00}, 0, 10},    // display on
    {0x51, (uint8_t[]){0xFF}, 1, 0},     // brightness full
};

void lcd_init(esp_lcd_panel_handle_t *out_panel)
{
    const spi_bus_config_t buscfg = SH8601_PANEL_BUS_QSPI_CONFIG(
        PIN_LCD_PCLK, PIN_LCD_D0, PIN_LCD_D1, PIN_LCD_D2, PIN_LCD_D3,
        LCD_H_RES * LCD_V_RES * LCD_BPP / 8);
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io = NULL;
    const esp_lcd_panel_io_spi_config_t io_config =
        SH8601_PANEL_IO_QSPI_CONFIG(PIN_LCD_CS, NULL, NULL);
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io));

    const sh8601_vendor_config_t vendor_cfg = {
        .init_cmds      = init_cmds,
        .init_cmds_size = sizeof(init_cmds) / sizeof(init_cmds[0]),
        .flags = { .use_qspi_interface = 1 },
    };
    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = LCD_BPP,
        .vendor_config  = (void *)&vendor_cfg,
    };

    esp_lcd_panel_handle_t panel = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_sh8601(io, &panel_cfg, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));
    *out_panel = panel;
}
```

The vendor examples run the QSPI bus at up to **75 MHz** on SPI2_HOST. If you see tearing artefacts or corrupted rows, drop to 40 MHz first before suspecting your drawing code.

### 6.3 Brightness

There is no PWM backlight. Write display command **`0x51`** with `0x00` (off) to `0xFF` (maximum) — but two things have to be true first, or the write does nothing:

1. **`0x53` (WRCTRLD, BCTRL=1) must have been sent during init** (§6.2). Without it the panel ignores `0x51` entirely.
2. **The command needs QSPI framing.** `esp_lcd_new_panel_sh8601()`'s internal `tx_param()` helper adds this automatically for the panel's own init sequence and control calls, but that helper is `static` — not exposed outside `esp_lcd_sh8601.c`. Any command you send yourself at runtime (like a brightness change from your own code) has to add the framing by hand:

```c
// opcode 0x02 (write) in bits 31:24, command in bits 15:8 — same framing
// esp_lcd_sh8601.c applies internally, replicated here because tx_param()
// isn't public.
uint8_t level = 0x80;
uint32_t qspi_cmd = (0x02UL << 24) | ((uint32_t)0x51 << 8);
esp_lcd_panel_io_tx_param(io, qspi_cmd, &level, 1);
```

Calling `esp_lcd_panel_io_tx_param(io, 0x51, &level, 1)` directly, without the framing, sends a malformed transaction on QSPI — it will not work on this interface.

With `Arduino_GFX`, the wrapper is `gfx->Display_Brightness(i)` for `i` in 0–255 (it handles the framing for you).

Because it's AMOLED, black pixels genuinely draw no current. A dark UI measurably extends battery life — this is not true of the LCD boards in the same family.

### 6.4 Orientation

The panel is natively **536 wide × 240 tall** (landscape). Waveshare ships a separate `LVGL_Test_90` example for the rotated case, because rotating a QSPI panel in software costs a full-frame transform per refresh. If your UI is portrait, set the rotation once at init rather than per-frame.

### 6.5 Tearing (TE)

`TE` on GPIO9 is the panel's frame-sync output; the RM67162 datasheet describes it as a tearing-effect output that synchronises the host to frame writes, and notes it must be **activated by software command** — until then the pin just sits low. Most projects ignore it and accept occasional tearing.

Note the conflict: on V2 boards GPIO9 is the microSD clock. You cannot use hardware TE and the SD card simultaneously on V2 hardware.

### 6.6 LVGL

With 8 MB of PSRAM you can afford generous buffers. Two partial buffers of 1/10th screen each is a good starting point; a full double buffer (536 × 240 × 2 bytes × 2 = ~515 KB) fits comfortably in PSRAM but pushes DMA traffic hard. Keep LVGL's draw buffers in **internal** RAM where possible and the framebuffer in PSRAM.

---

## 7. Touch (FT3168)

Only on the touch SKUs. It shares GPIO39/40 with the IMU, so initialise the I²C bus once.

Minimal read, matching the vendor driver:

```c
#define FT3168_ADDR  0x38

// Wake into normal mode at startup: write 0x00 to register 0x00.

bool touch_read(uint16_t *x, uint16_t *y)
{
    uint8_t n = 0, buf[4];
    i2c_read_reg(FT3168_ADDR, 0x02, &n, 1);   // number of touch points
    if (!n) return false;

    i2c_read_reg(FT3168_ADDR, 0x03, buf, 4);
    *y = ((uint16_t)(buf[0] & 0x0F) << 8) | buf[1];
    *x = ((uint16_t)(buf[2] & 0x0F) << 8) | buf[3];

    if (*x > 536) *x = 536;
    if (*y > 240) *y = 240;
    *y = 240 - *y;   // controller's Y increases downward; flip to match panel/LVGL Y
    return true;
}
```

Three things worth knowing:

- The controller returns **Y first, then X** — swapped relative to what you'd expect. That's why the code above looks backwards.
- **The Y axis needs flipping** (last line above) to line up with the display's coordinate space — without it, touches register vertically mirrored versus what's on screen.
- **TP_RST is not independently controllable.** It's tied to the display reset net, so resetting touch resets the panel. Pass `-1` for the reset pin in any driver that asks. The interrupt on GPIO41 works normally and is worth using instead of polling.

The controller supports 5 simultaneous points; the snippet above reads only the first. Points 2–5 follow at 6-byte strides from register 0x03.

---

## 8. IMU (QMI8658C)

I²C address **0x6B** — the schematic pulls the SDO/SA0 pin (pin 1) high. If you ever read 0x6A instead, you're looking at a different board.

Accelerometer sensitivity, from the QMI8658C datasheet (16-bit output):

| Range | LSB/g |
|---|---|
| ±2 g | 16384 |
| ±4 g | 8192 |
| ±8 g | 4096 |
| ±16 g | 2048 |

Other figures from the same document: noise density 150 µg/√Hz at 32 Hz in high-resolution mode, cross-axis sensitivity ±1 %, initial offset tolerance ±100 mg at board level, and a **system turn-on time of 150 ms** from reset or power-down. Don't read the device inside that window — you'll get zeros or stale values and conclude the sensor is dead.

Basic startup sequence:

1. Read `WHO_AM_I` (register 0x00) — expect **0x05**.
2. Write `CTRL1` = 0x60 to set 4-wire/auto-increment addressing.
3. Set `CTRL2` (accel ODR + range) and `CTRL3` (gyro ODR + range).
4. Write `CTRL7` to enable accel and/or gyro (`0x03` for both).
5. Poll `STATUS0` bit 0, or use the interrupt lines.

> **Value differs from this project's working code:** `imu_qmi8658.c` in this repo writes **`0x40`** to `CTRL1` (register `0x02`), not `0x60`, and its accelerometer-only demo is confirmed working on real hardware. Bit-level correctness of `0x60` vs `0x40` hasn't been re-checked against the QMI8658C register datasheet here — if you hit odd IMU behavior, this is a value worth comparing against §CTRL1 in the datasheet directly.

The two interrupt pins are wired to GPIO45 and GPIO46. `CTRL8` bit 6 selects whether data-ready is routed to INT1 or INT2. The chip also has built-in motion engines — pedometer, any-motion, no-motion, significant-motion, tap — which are worth using if you're building anything battery-powered, since they let the ESP32-S3 deep-sleep and be woken by the IMU rather than polling it.

---

## 9. microSD — check your revision first

This is the one place where the two board revisions genuinely differ, and code for one will not work on the other.

**V2 (current) — SDMMC, 1-bit:**

```c
sdmmc_host_t host = SDMMC_HOST_DEFAULT();
sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
slot.width = 1;
slot.clk = GPIO_NUM_9;
slot.cmd = GPIO_NUM_42;
slot.d0  = GPIO_NUM_8;

esp_vfs_fat_sdmmc_mount_config_t mount = {
    .format_if_mount_failed = true,
    .max_files = 5,
    .allocation_unit_size = 512,
};
esp_vfs_fat_sdmmc_mount("/sd_card", &host, &slot, &mount, &card);
```

**V1 (earlier) — SPI on SPI3_HOST:** CLK = 47, MOSI = 42, MISO = 8, CS = 9.

**How to tell which you have:** the V1 schematic shares the card's clock with the display clock (`SD_CLK` tied to `WRX_SCL`, GPIO47) and its data-out with the display's `SDO` (GPIO8). V2 moves the clock to GPIO9. If you're unsure, try the V2 SDMMC config first — it's what current boards ship with — and fall back to SPI if the card won't mount.

> **Settled since this guide was written:** the repo now has real, hardware-tested SD code — `libraries/AMOLED191_SD` — and it implements the **V2 SDMMC 1-bit** wiring (CLK=9, CMD=42, D0=8) described above, not the V1 SPI one. V1 boards are not supported by that library: if `SDCard_Init()` fails on a card you know is good and FAT32, suspect a V1 board. The two wirings are not interchangeable and cannot be told apart at runtime.

Either way, **GPIO8 is shared with the display's SDO line** and **GPIO42 carries a 10 kΩ pull-up (R14)**. In practice this is fine because the display is write-only in normal operation, but it does mean you should not try to read display registers while an SD transfer is in flight.

Note also that the display and SD card sit on the same physical bus on V1. Keep SD access off the LVGL rendering task, or you'll see frame stalls.

---

## 10. Power and battery

### Charging

USB-C in → **PL4054** linear charger → 3.7 V LiPo on the MX1.25 header. Charge current is set by the programming resistor (R1, 1 kΩ on the schematic). An **APM2307** load switch plus a 1N4148 handles the USB/battery handover, so you can hot-swap between USB and battery without the board browning out.

### Reading battery voltage

The battery goes through a **100 kΩ / 100 kΩ divider** (R6/R8) into **GPIO1 = ADC1_CH0**. Because the divider is 1:1, multiply the measured voltage by 2.

```c
// ESP-IDF, matching the vendor example
adc_oneshot_unit_init_cfg_t unit_cfg = { .unit_id = ADC_UNIT_1 };
adc_oneshot_new_unit(&unit_cfg, &adc1);

adc_oneshot_chan_cfg_t ch_cfg = {
    .bitwidth = ADC_BITWIDTH_12,
    .atten    = ADC_ATTEN_DB_12,   // ~0–3.3 V range
};
adc_oneshot_config_channel(adc1, ADC_CHANNEL_0, &ch_cfg);

int raw, mv;
adc_oneshot_read(adc1, ADC_CHANNEL_0, &raw);
adc_cali_raw_to_voltage(cali_handle, raw, &mv);
float vbat = 0.001f * mv * 2.0f;   // ×2 for the divider
```

Use the curve-fitting calibration scheme (`adc_cali_create_scheme_curve_fitting`). The raw-to-voltage approximation `raw * 3.3 / 4096` is off by 100 mV or more across the range, which is the difference between "battery low" and "battery fine" at the top of a LiPo discharge curve.

A full 4.2 V battery reads 2.1 V at the pin — comfortably inside ADC1's range at 12 dB attenuation.

### Power budget notes

The AMOLED dominates consumption and scales with how many pixels are lit. Wi-Fi transmit peaks are short but sharp; a battery with high internal resistance can brown out the panel supply during association. If the screen flickers only when Wi-Fi connects, that's the cause.

---

## 11. Wi-Fi and Bluetooth

Standard ESP32-S3 radio — nothing board-specific in software. Station mode:

```c
esp_netif_init();
esp_event_loop_create_default();
esp_netif_create_default_wifi_sta();

wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
esp_wifi_init(&cfg);

wifi_config_t sta = {
    .sta = { .ssid = "your-ssid", .password = "your-password" },
};
esp_wifi_set_mode(WIFI_MODE_STA);
esp_wifi_set_config(WIFI_IF_STA, &sta);
esp_wifi_start();
esp_wifi_connect();
```

Arduino is the usual `WiFi.begin(ssid, pass)`.

### Antenna

The board has a PCB antenna by default. **R3 (0 Ω)** selects between it and the IPEX/U.FL connector — moving that resistor is the documented way to switch to an external antenna. Keep the antenna corner clear of the metal case and of your hand; this is a small board and detuning is easy to cause and hard to diagnose.

Coexistence: Wi-Fi and BLE share one radio. Running both halves your effective throughput and adds latency jitter — relevant if you're streaming display content over the network.

---

## 12. Expansion headers

Two 20-pin headers (H1, H2) in the Raspberry Pi Pico footprint, fitted on the `-M` SKUs. H1 begins VSYS, GND, GP44, GP43, GP0, GP1, GND, GP2, GP3, GP4, GND, GP10…

Genuinely free pins: **2, 3, 4, 10, 11, 12, 13, 14, 15, 16, 21, 38** (see note in §2 — this repo's own docs currently omit 38).

Also present but already committed: 0 (BOOT), 1 (battery sense), 19/20 (USB), 39/40 (I²C), 43/44 (UART0).

And exposed but **unusable** — 26, 33, 34, 35, 36, 37 (PSRAM, see §3).

The Pico-compatible footprint means mechanical compatibility with Pico add-ons, but **not** electrical compatibility: pin functions differ entirely. Check the net names before plugging in a HAT.

---

## 13. Gotcha checklist

Things that cost people an afternoon on this board:

1. **PSRAM set to "Disabled" or "QSPI"** instead of OPI → 8 MB of RAM silently missing.
2. **Using GPIO33–37 or GPIO26** because the header exposes them → PSRAM corruption, boot loops.
3. **Hunting for an RM67162 QSPI driver** → use `esp_lcd_sh8601`.
4. **Looking for a backlight pin** → there isn't one; use command `0x51`.
5. **Wrong SD wiring for your revision** → try SDMMC 1-bit (V2) before SPI (V1).
6. **Touch X and Y swapped** → the FT3168 returns Y first.
7. **Trying to reset touch independently** → TP_RST is tied to display reset; pass `-1`.
8. **Reading the IMU within 150 ms of power-up** → returns nothing useful.
9. **Forgetting the ×2 battery divider** → every reading looks half-flat.
10. **TE and microSD both wanted on V2** → both are GPIO9; pick one.
11. **Board stops enumerating** → BOOT + RESET, release BOOT.

---

## 14. Source notes

Pin assignments were read from the board schematic in this project and independently confirmed against Waveshare's published example sources (`waveshareteam/ESP32-S3-AMOLED-1.91`), specifically `pins_config.h`, `sd_card_bsp.cpp`, `i2c_bsp.cpp`, `touch_bsp.c`, `adc_bsp.c`, and the LVGL example's LCD defines. Where the two agreed, the value is stated plainly above.

Component-level figures come from the datasheets in this project: ESP32-S3 Series Datasheet v2.2 (pin functions, PSRAM/flash pin mapping, USB Serial/JTAG behaviour), QMI8658C rev 0.9 (accelerometer specifications, turn-on time), and RM67162 rev 0.5 (interface modes, TE behaviour).

One caveat worth repeating: the RM67162 document is rev 0.5 and predates QSPI support on this controller family, so it will not help you debug QSPI timing. For that, the `esp_lcd_sh8601` component source is the better reference.
