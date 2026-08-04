/**
 * AMOLED191_IMU.h
 *
 * Mini-driver per l'IMU QMI8658 onboard (6 assi) della Waveshare
 * ESP32-S3-AMOLED-1.91. L'IMU e' sullo STESSO bus I2C del touch
 * (I2C_NUM_0, SDA=GPIO40, SCL=GPIO39): imu_init() porta su il bus tramite
 * Core_I2CBusInit() (AMOLED191_Core), idempotente — funziona indipendentemente
 * dal fatto che AMOLED191_Touch sia stato inizializzato prima, dopo, o per
 * niente.
 */

#ifndef AMOLED191_IMU_H
#define AMOLED191_IMU_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Inizializza l'accelerometro. Ritorna false se il chip non risponde
 * (WHO_AM_I != 0x05): in tal caso prova l'indirizzo alternativo (vedi .cpp). */
bool imu_init(void);

/* Legge l'accelerazione nei 3 assi, in unita' di g (1 g = 9.81 m/s^2).
 * Ritorna false se la lettura I2C fallisce. */
bool imu_read_accel(float *ax, float *ay, float *az);

#ifdef __cplusplus
}
#endif

#endif // AMOLED191_IMU_H
