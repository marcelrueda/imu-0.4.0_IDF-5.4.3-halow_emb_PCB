#ifndef I2C_HELPER_H
#define I2C_HELPER_H

#include "helper_i2c.h"

// Definiciones para el bus I2C0 (display/ Reloj)
#define I2C0_SCL 2          // GPIO para línea SCL
#define I2C0_SDA 3          // GPIO para línea SDA
#define I2C0_FREQ_HZ 400000 // Frecuencia I2C a 400 kHz

// Definiciones para el bus I2C1 (ICM42670)
#define I2C1_SCL 2          // GPIO para línea SCL
#define I2C1_SDA 3          // GPIO para línea SDA
#define I2C1_FREQ_HZ 400000 // Frecuencia I2C a 400 kHz

#define I2C_MASTER_TIMEOUT_MS 1000  // Timeout en milisegundos
#define I2C_MASTER_TX_BUF_DISABLE 0 // Deshabilitar buffer de transmisión
#define I2C_MASTER_RX_BUF_DISABLE 0 // Deshabilitar buffer de recepción

extern HelperI2C i2c_bus0; // Declarar externamente la variable para compartirla entre archivos
extern HelperI2C i2c_bus1; // Declarar externamente la variable para compartirla entre archivos

// Prototipos de funciones
esp_err_t i2c0_master_init(void); // Inicializar el bus I2C0 maestro
void i2c1_master_init(void);      // Inicializar el bus I2C1 maestro
#endif                            // I2C_HELPER_H

/*

#define I2C0_SCL 18         // GPIO para línea SCL
#define I2C0_SDA 17         // GPIO para línea SDA
#define I2C0_FREQ_HZ 400000 // Frecuencia I2C a 400 kHz

#define I2C1_SCL 18         // GPIO para línea SCL
#define I2C1_SDA 17         // GPIO para línea SDA
#define I2C1_FREQ_HZ 400000 // Frecuencia I2C a 400 kHz

*/