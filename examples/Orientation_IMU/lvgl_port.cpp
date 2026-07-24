/**
 * lvgl_port.cpp
 *
 * Tutto il boilerplate di basso livello: SPI/QSPI, pannello SH8601, touch,
 * LVGL, tick timer, mutex e task di rendering. Lo sketch non deve toccare
 * nulla qui dentro: usa solo le funzioni dichiarate in lvgl_port.h.
 */

#include "lvgl_port.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "driver/spi_master.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_heap_caps.h"
#include "esp_err.h"

#include "lvgl.h"
#include "esp_lcd_sh8601.h"
#include "touch_bsp.h"

// ---------------------------------------------------------------------------
// Pin display (QSPI) e risoluzione
// ---------------------------------------------------------------------------
#define LCD_HOST            SPI2_HOST
#define LCD_BIT_PER_PIXEL   16

#define PIN_LCD_CS          GPIO_NUM_6
#define PIN_LCD_PCLK        GPIO_NUM_47
#define PIN_LCD_DATA0       GPIO_NUM_18
#define PIN_LCD_DATA1       GPIO_NUM_7
#define PIN_LCD_DATA2       GPIO_NUM_48
#define PIN_LCD_DATA3       GPIO_NUM_5
#define PIN_LCD_RST         GPIO_NUM_17

#define LCD_H_RES           536
#define LCD_V_RES           240

// QSPI: opcode di scrittura/lettura comando (vedi esp_lcd_sh8601.c)
#define LCD_QSPI_WRITE_CMD  (0x02UL)
#define LCD_QSPI_READ_CMD   (0x03UL)

// ---------------------------------------------------------------------------
// Parametri LVGL
//   - LVGL_BUF_LINES: piu' grande = piu' fluido ma piu' RAM DMA interna.
//     Con WiFi attivo la RAM interna e' preziosa: /4 e' un buon compromesso,
//     scendi a /8 se ti serve liberare memoria per lo stack WiFi.
// ---------------------------------------------------------------------------
#define LVGL_BUF_LINES          (LCD_V_RES / 4)   // 60 righe per buffer
#define LVGL_TICK_PERIOD_MS     2
#define LVGL_TASK_MAX_DELAY_MS  500
#define LVGL_TASK_MIN_DELAY_MS  1
#define LVGL_TASK_STACK_SIZE    (4 * 1024)
#define LVGL_TASK_PRIORITY      2

// ---------------------------------------------------------------------------
// Stato interno
// ---------------------------------------------------------------------------
static SemaphoreHandle_t        lvgl_mux    = NULL;
static esp_lcd_panel_io_handle_t s_io_handle = NULL;   // per inviare comandi a runtime
static lv_disp_draw_buf_t       disp_buf;
static lv_disp_drv_t            disp_drv;
static lv_indev_drv_t           indev_drv;

// ---------------------------------------------------------------------------
// Sequenza init SH8601
// ---------------------------------------------------------------------------
static const sh8601_lcd_init_cmd_t lcd_init_cmds[] = {
    {0x11, (uint8_t[]){0x00}, 0, 120},                  // Sleep Out
    {0x36, (uint8_t[]){0xF0}, 1,   0},                  // MADCTL (landscape)
    {0x3A, (uint8_t[]){0x55}, 1,   0},                  // Pixel format RGB565
    {0x53, (uint8_t[]){0x20}, 1,   0},                  // WRCTRLD: BCTRL=1 (abilita 0x51)
    {0x2A, (uint8_t[]){0x00, 0x00, 0x02, 0x17}, 4, 0},  // Colonne 0..535
    {0x2B, (uint8_t[]){0x00, 0x00, 0x00, 0xEF}, 4, 0},  // Righe   0..239
    {0x29, (uint8_t[]){0x00}, 0,  10},                  // Display On
    {0x51, (uint8_t[]){0xFF}, 1,   0},                  // Luminosita' massima
};

// ---------------------------------------------------------------------------
// Callback LVGL
// ---------------------------------------------------------------------------
static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *px)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)drv->user_data;
    esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1,
                              area->x2 + 1, area->y2 + 1, px);
}

static bool flush_ready_cb(esp_lcd_panel_io_handle_t io,
                           esp_lcd_panel_io_event_data_t *e, void *ctx)
{
    lv_disp_flush_ready((lv_disp_drv_t *)ctx);
    return false;
}

static void lvgl_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    uint16_t x, y;
    if (getTouch(&x, &y)) {
        data->point.x = x;
        data->point.y = y;
        data->state   = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void lvgl_tick_cb(void *arg)
{
    lv_tick_inc(LVGL_TICK_PERIOD_MS);   // safe da chiamare fuori dal task
}

// ---------------------------------------------------------------------------
// Mutex pubblico
// ---------------------------------------------------------------------------
bool lvgl_lock(int timeout_ms)
{
    const TickType_t ticks = (timeout_ms == -1) ? portMAX_DELAY
                                                : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(lvgl_mux, ticks) == pdTRUE;
}

void lvgl_unlock(void)
{
    xSemaphoreGive(lvgl_mux);
}

// ---------------------------------------------------------------------------
// Invio comandi al pannello via QSPI
// ---------------------------------------------------------------------------
void lcd_command(uint8_t cmd, const uint8_t *data, size_t len)
{
    // Framing QSPI del SH8601/RM67162: opcode 0x02 nei bit 31:24,
    // comando nei bit 15:8 -> sul bus diventa [0x02][0x00][CMD][0x00][DATA...]
    uint32_t qspi_cmd = (LCD_QSPI_WRITE_CMD << 24) | ((uint32_t)cmd << 8);
    esp_lcd_panel_io_tx_param(s_io_handle, qspi_cmd, data, len);
}

void lcd_set_brightness(uint8_t level)
{
    lcd_command(0x51, &level, 1);
}

bool lcd_read_register(uint8_t cmd, uint8_t *data, size_t len)
{
    // Lettura QSPI: opcode 0x03 nei bit 31:24, comando nei bit 15:8.
    uint32_t qspi_cmd = (LCD_QSPI_READ_CMD << 24) | ((uint32_t)cmd << 8);
    return esp_lcd_panel_io_rx_param(s_io_handle, qspi_cmd, data, len) == ESP_OK;
}

// ---------------------------------------------------------------------------
// Task di rendering: tiene il lock mentre gira lv_timer_handler()
// ---------------------------------------------------------------------------
static void lvgl_task(void *arg)
{
    uint32_t delay_ms = LVGL_TASK_MAX_DELAY_MS;
    while (1) {
        if (lvgl_lock(-1)) {
            delay_ms = lv_timer_handler();
            lvgl_unlock();
        }
        if (delay_ms > LVGL_TASK_MAX_DELAY_MS) delay_ms = LVGL_TASK_MAX_DELAY_MS;
        else if (delay_ms < LVGL_TASK_MIN_DELAY_MS) delay_ms = LVGL_TASK_MIN_DELAY_MS;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

// ---------------------------------------------------------------------------
// Init pubblica
// ---------------------------------------------------------------------------
void lvgl_port_init(void)
{
    // --- Bus QSPI ---
    const spi_bus_config_t buscfg = SH8601_PANEL_BUS_QSPI_CONFIG(
        PIN_LCD_PCLK, PIN_LCD_DATA0, PIN_LCD_DATA1, PIN_LCD_DATA2, PIN_LCD_DATA3,
        LCD_H_RES * LCD_V_RES * LCD_BIT_PER_PIXEL / 8);
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    // --- Panel IO ---
    esp_lcd_panel_io_handle_t io_handle = NULL;
    const esp_lcd_panel_io_spi_config_t io_config =
        SH8601_PANEL_IO_QSPI_CONFIG(PIN_LCD_CS, flush_ready_cb, &disp_drv);
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));
    s_io_handle = io_handle;   // salva per lcd_command() / lcd_set_brightness()

    // --- Driver pannello ---
    sh8601_vendor_config_t vendor_config = {
        .init_cmds      = lcd_init_cmds,
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
        .flags = { .use_qspi_interface = 1 },
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = LCD_BIT_PER_PIXEL,
        .vendor_config  = &vendor_config,
    };
    esp_lcd_panel_handle_t panel_handle = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_sh8601(io_handle, &panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    // --- Touch + LVGL ---
    Touch_Init();
    lv_init();

    // --- Buffer di disegno (DMA, doppio buffer) ---
    lv_color_t *buf1 = (lv_color_t *)heap_caps_malloc(
        LCD_H_RES * LVGL_BUF_LINES * sizeof(lv_color_t), MALLOC_CAP_DMA);
    lv_color_t *buf2 = (lv_color_t *)heap_caps_malloc(
        LCD_H_RES * LVGL_BUF_LINES * sizeof(lv_color_t), MALLOC_CAP_DMA);
    assert(buf1 && buf2);
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, LCD_H_RES * LVGL_BUF_LINES);

    // --- Display driver ---
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res   = LCD_H_RES;
    disp_drv.ver_res   = LCD_V_RES;
    disp_drv.flush_cb  = lvgl_flush_cb;
    disp_drv.draw_buf  = &disp_buf;
    disp_drv.user_data = panel_handle;
    lv_disp_t *disp = lv_disp_drv_register(&disp_drv);

    // --- Tick LVGL via esp_timer ---
    const esp_timer_create_args_t tick_args = {
        .callback = &lvgl_tick_cb,
        .name     = "lvgl_tick"
    };
    esp_timer_handle_t tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, LVGL_TICK_PERIOD_MS * 1000));

    // --- Touch input device ---
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.disp    = disp;
    indev_drv.read_cb = lvgl_touch_cb;
    lv_indev_drv_register(&indev_drv);

    // --- Mutex + task di rendering ---
    lvgl_mux = xSemaphoreCreateMutex();
    assert(lvgl_mux);
    xTaskCreate(lvgl_task, "LVGL", LVGL_TASK_STACK_SIZE, NULL, LVGL_TASK_PRIORITY, NULL);
}
