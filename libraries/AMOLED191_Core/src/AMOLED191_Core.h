/**
 * AMOLED191_Core.h
 *
 * Primitive condivise tra i moduli hardware della Waveshare
 * ESP32-S3-Touch-AMOLED-1.91: al momento, solo il bring-up del bus I2C
 * condiviso tra touch (AMOLED191_Touch) e IMU (AMOLED191_IMU).
 */

#ifndef AMOLED191_CORE_H
#define AMOLED191_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

// Bus I2C condiviso da touch FT3168 e IMU QMI8658 su questa board.
#define AMOLED191_CORE_I2C_PORT      0            // I2C_NUM_0
#define AMOLED191_CORE_I2C_SDA       40           // GPIO_NUM_40
#define AMOLED191_CORE_I2C_SCL       39           // GPIO_NUM_39
#define AMOLED191_CORE_I2C_CLOCK_HZ  (300 * 1000) // 300 kHz

/**
 * Inizializza il bus I2C condiviso (I2C_NUM_0, pin/clock sopra), se non lo e'
 * gia'. Idempotente: chiamabile da AMOLED191_Touch e AMOLED191_IMU in qualunque
 * ordine, o anche da uno solo dei due, senza reinstallare il driver due volte.
 *
 * NOTA: la guardia di idempotenza e' un semplice static bool, non protetta da
 * mutex. Sicura per ogni uso reale in questo repo (Touch_Init()/imu_init()
 * vengono chiamate in sequenza da setup(), single-threaded, prima che parta
 * qualunque task). Se in futuro un modulo (es. WiFi/SD) la chiamasse da un
 * task in background concorrente con setup(), va aggiunta una protezione
 * (mutex/portMUX) prima di riusarla in quel contesto.
 */
void Core_I2CBusInit(void);

#ifdef __cplusplus
}
#endif

#endif // AMOLED191_CORE_H
