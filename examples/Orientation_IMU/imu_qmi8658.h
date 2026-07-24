/**
 * imu_qmi8658.h
 *
 * Mini-driver per l'IMU QMI8658 onboard (6 assi) della Waveshare
 * ESP32-S3-AMOLED-1.91. L'IMU e' sullo STESSO bus I2C del touch
 * (I2C_NUM_0, SDA=GPIO40, SCL=GPIO39): questo modulo NON installa il driver
 * I2C, riusa quello gia' avviato da Touch_Init() dentro lvgl_port_init().
 *
 * Ordine in setup(): prima lvgl_port_init() (che chiama Touch_Init), poi imu_init().
 */

#ifndef IMU_QMI8658_H
#define IMU_QMI8658_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Inizializza l'accelerometro. Ritorna false se il chip non risponde
 * (WHO_AM_I != 0x05): in tal caso prova l'indirizzo alternativo (vedi .c). */
bool imu_init(void);

/* Legge l'accelerazione nei 3 assi, in unita' di g (1 g = 9.81 m/s^2).
 * Ritorna false se la lettura I2C fallisce. */
bool imu_read_accel(float *ax, float *ay, float *az);

#ifdef __cplusplus
}
#endif

#endif // IMU_QMI8658_H
