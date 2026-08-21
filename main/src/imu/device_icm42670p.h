// device_icm42670p.h
// MODULO: Driver para el sensor ICM42670P (Acelerómetro + Giroscopio) vía SPI
// Autor: Carlos A Vargas
// carlos_vargas_c@outlook.com
// Versión: 0.1 - Agosto 2024

#ifndef DEVICE_ICM42670P_H
#define DEVICE_ICM42670P_H

#include "esp_err.h"
#include <stdint.h>
#include "driver/spi_master.h"

#define ICM42670P_SPI_MISO 42
#define ICM42670P_SPI_MOSI 41
#define ICM42670P_SPI_SCLK 40
#define ICM42670P_SPI_CS 21//37




// --- Registros claves ---
#define ICM42670P_REG_MCLK_RDY 0x00
#define ICM42670P_REG_WHO_AM_I 0x75
#define ICM42670P_REG_PWR_MGMT0 0x1F
#define ICM42670P_REG_TEMP_DATA1 0x09
#define ICM42670P_REG_TEMP_DATA0 0x0A
#define ICM42670P_REG_ACCEL_DATA_X1 0x0B // burst hasta 0x16
#define ICM42670P_REG_GYRO_CONFIG0 0x20
#define ICM42670P_REG_ACCEL_CONFIG0 0x21

// --- Configuraciones ---
#define ICM42670P_WHOAMI_ID 0x67              // Valor esperado en WHOAMI
#define ICM42670P_PWR_MODE 0x0F               // accel+gyro en modo LN
#define ICM42670P_GYRO_CFG_2000DPS_100HZ 0x69 // Gyro: ±2000 dps, ODR=100 Hz (0b1001)
#define ICM42670P_ACCEL_CFG_2G_100HZ 0x69     // Accel: ±2 g (11 << 5 = 0x60), ODR=100 Hz (0x09)

// Escalas
#define ICM42670P_GYRO_SCALE 131.0f
#define ICM42670P_ACCEL_SCALE 16384.0f // LSB/g @ ±2 g
#define ICM42670P_TEMP_SCALE 132.48f   // LSB/°C
#define ICM42670P_TEMP_OFFSET 25.0f    // °C

// --- Registros ---
#define ICM42670P_REG_GYRO_CONFIG1  0x23
#define ICM42670P_REG_ACCEL_CONFIG1 0x24
// --- Configuraciones ---
#define ICM42670P_GYRO_CFG1_BW_16HZ  0x37 // reset 0x31 + UI_FILT_BW=111 (16 Hz)
#define ICM42670P_ACCEL_CFG1_BW_16HZ 0x47 // reset 0x41 + UI_FILT_BW=111 (16 Hz)

// --- estructura de datos ---
typedef struct
{
    float accel[3];
    float gyro[3];
    float temperature;
} icm42670p_data_t;

// --- offsets de calibración ---
typedef struct
{
    float accel[3];
    float gyro[3];
} icm42670p_calib_t;

// --- Prototipos de funciones ---
spi_device_handle_t icm_get_handle(void);                                                      // Exponer el handle SPI
esp_err_t icm42670p_init(spi_device_handle_t *spi); 


// Inicializa el sensor y devuelve el handle SPI
esp_err_t icm42670p_read_sensor_data(spi_device_handle_t spi, icm42670p_data_t *data);         // Lee datos del sensor
esp_err_t icm42670p_calibrate(spi_device_handle_t spi, icm42670p_calib_t *calib, int samples); // Calibra el sensor
esp_err_t icm42670p_setup(void);                                                               // Configuración inicial y calibración
void app_ICM42670P(void);                                                                      // Función de prueba de funcionamiento
#endif                                                                                         // DEVICE_ICM42670P_H
