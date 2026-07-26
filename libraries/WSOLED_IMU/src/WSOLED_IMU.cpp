/**
 * WSOLED_IMU.cpp
 *
 * Lettura accelerometro QMI8658 su I2C (bus condiviso col touch, portato su
 * da Core_I2CBusInit()). Configurazione minima: accelerometro a fondo scala
 * +/-2 g (massima risoluzione per il calcolo dell'inclinazione).
 */

#include "WSOLED_IMU.h"
#include "WSOLED_Core.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"

#define QMI_I2C_PORT   ((i2c_port_t)WSOLED_CORE_I2C_PORT)
#define QMI_ADDR       0x6B        // se WHO_AM_I fallisce, prova 0x6A
#define QMI_TIMEOUT    pdMS_TO_TICKS(100)

// Registri QMI8658
#define QMI_WHO_AM_I   0x00        // -> 0x05
#define QMI_CTRL1      0x02        // interfaccia / auto-increment
#define QMI_CTRL2      0x03        // accel: fondo scala + ODR
#define QMI_CTRL7      0x08        // enable sensori (bit0 = accel)
#define QMI_AX_L       0x35        // primo registro dati accel (6 byte)

// +/-2 g -> 16384 LSB/g
#define QMI_ACC_SENS   (1.0f / 16384.0f)

static bool imu_read(uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_master_write_read_device(QMI_I2C_PORT, QMI_ADDR,
                                        &reg, 1, buf, len, QMI_TIMEOUT) == ESP_OK;
}

static bool imu_write(uint8_t reg, uint8_t val)
{
    uint8_t b[2] = { reg, val };
    return i2c_master_write_to_device(QMI_I2C_PORT, QMI_ADDR,
                                      b, sizeof(b), QMI_TIMEOUT) == ESP_OK;
}

bool imu_init(void)
{
    Core_I2CBusInit();

    uint8_t id = 0;
    if (!imu_read(QMI_WHO_AM_I, &id, 1) || id != 0x05) {
        return false;   // chip non trovato (controlla indirizzo / bus I2C attivo)
    }

    imu_write(QMI_CTRL1, 0x40);   // address auto-increment per la lettura a blocco
    imu_write(QMI_CTRL2, 0x05);   // accel +/-2 g, ODR ~250 Hz
    imu_write(QMI_CTRL7, 0x01);   // abilita solo l'accelerometro
    vTaskDelay(pdMS_TO_TICKS(20));
    return true;
}

bool imu_read_accel(float *ax, float *ay, float *az)
{
    uint8_t b[6];
    if (!imu_read(QMI_AX_L, b, 6)) {
        return false;
    }
    int16_t rx = (int16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8));
    int16_t ry = (int16_t)((uint16_t)b[2] | ((uint16_t)b[3] << 8));
    int16_t rz = (int16_t)((uint16_t)b[4] | ((uint16_t)b[5] << 8));
    *ax = rx * QMI_ACC_SENS;
    *ay = ry * QMI_ACC_SENS;
    *az = rz * QMI_ACC_SENS;
    return true;
}
