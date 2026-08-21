// device_icm42670p.c
// MODULO: Driver para el sensor ICM42670P (Acelerómetro + Giroscopio) vía SPI
// Autor: Carlos A Vargas
// carlos_vargas_c@outlook.com
// Versión: 0.1 - Agosto 2024

#include "bus/bus_spi.h"
#include "device_icm42670p.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>
#include "driver/gpio.h"

static const char *TAG = "ICM42670P";            // etiqueta para logs
static spi_device_handle_t spi_handle_ICM42670P; // handle SPI global
static icm42670p_calib_t g_calib = {0};          // offsets globales

// Exponer el handle SPI
spi_device_handle_t icm_get_handle(void)
{
    return spi_handle_ICM42670P;
}
// Escritura de un registro
esp_err_t icm42670p_calibrate(spi_device_handle_t spi, icm42670p_calib_t *calib, int samples)
{
    icm42670p_data_t data;
    float sum_accel[3] = {0}, sum_gyro[3] = {0};

    ESP_LOGI(TAG, "Iniciando calibracion con %d muestras...", samples);

    // 🔧 Descarta primeras lecturas (sensor calentando)
    for (int i = 0; i < 50; i++)
    {
        icm42670p_read_sensor_data(spi, &data);
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    for (int i = 0; i < samples; i++)
    {
        if (icm42670p_read_sensor_data(spi, &data) == ESP_OK)
        {
            sum_accel[0] += data.accel[0];
            sum_accel[1] += data.accel[1];
            sum_accel[2] += data.accel[2];

            sum_gyro[0] += data.gyro[0];
            sum_gyro[1] += data.gyro[1];
            sum_gyro[2] += data.gyro[2];
        }
        vTaskDelay(pdMS_TO_TICKS(10)); // 100 Hz -> 10 ms
    }

    // Promedios
    calib->accel[0] = sum_accel[0] / samples;
    calib->accel[1] = sum_accel[1] / samples;
    calib->accel[2] = (sum_accel[2] / samples) - 1.0f; // quitar gravedad en Z

    // Bias del giroscopio (reposo debe dar 0.0)
    calib->gyro[0] = sum_gyro[0] / samples;
    calib->gyro[1] = sum_gyro[1] / samples;
    calib->gyro[2] = sum_gyro[2] / samples;

    g_calib = *calib;

    ESP_LOGI(TAG, "Calibracion lista. Offsets aplicados: "
                  "ACC[X=%.3f Y=%.3f Z=%.3f] | GYRO[X=%.3f Y=%.3f Z=%.3f]",
             calib->accel[0], calib->accel[1], calib->accel[2],
             calib->gyro[0], calib->gyro[1], calib->gyro[2]);

    return ESP_OK;
}
// Escritura de un registro
static esp_err_t icm42670p_write_reg(spi_device_handle_t spi, uint8_t reg, uint8_t data)
{
    uint8_t tx[2] = {reg & 0x7F, data};
    spi_transaction_t t = {
        .length = 16,
        .tx_buffer = tx,
    };
    return spi_device_transmit(spi, &t);
}
// Lectura de múltiples registros
static esp_err_t icm42670p_read_regs(spi_device_handle_t spi, uint8_t reg, uint8_t *data, size_t len)
{
    uint8_t tx[16] = {0};
    uint8_t rx[16] = {0};

    tx[0] = reg | 0x80; // bit7=1 -> read
    spi_transaction_t t = {
        .length = (len + 1) * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    esp_err_t ret = spi_device_transmit(spi, &t);
    if (ret != ESP_OK)
        return ret;

    memcpy(data, &rx[1], len);
    return ESP_OK;
}
// Inicializa el sensor y devuelve el handle SPI
esp_err_t icm42670p_init(spi_device_handle_t *spi)
{
    esp_err_t ret;

    // --- Agregar el ICM42670P como dispositivo del bus ya inicializado ---
    ret = spi_bus_add_dev(SPI3_HOST, ICM42670P_SPI_CS, 10 * 1000 * 1000, &spi_handle_ICM42670P);
    if (ret != ESP_OK)
        return ret;

    *spi = spi_handle_ICM42670P;

    vTaskDelay(pdMS_TO_TICKS(10));

    // --- Verificar WHOAMI ---
    uint8_t whoami = 0;
    icm42670p_read_regs(spi_handle_ICM42670P, ICM42670P_REG_WHO_AM_I, &whoami, 1);
    ESP_LOGI(TAG, "WHOAMI = 0x%02X", whoami);
    if (whoami != ICM42670P_WHOAMI_ID)
        return ESP_FAIL;

    // --- Esperar MCLK_RDY ---
    uint8_t mclk = 0;
    for (int i = 0; i < 50; i++)
    {
        icm42670p_read_regs(spi_handle_ICM42670P, ICM42670P_REG_MCLK_RDY, &mclk, 1);
        if (mclk & 0x01)
            break;
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    // --- Configuración inicial ---
    icm42670p_write_reg(spi_handle_ICM42670P, ICM42670P_REG_PWR_MGMT0, ICM42670P_PWR_MODE);
    icm42670p_write_reg(spi_handle_ICM42670P, ICM42670P_REG_GYRO_CONFIG0, ICM42670P_GYRO_CFG_2000DPS_100HZ);
    icm42670p_write_reg(spi_handle_ICM42670P, ICM42670P_REG_ACCEL_CONFIG0, ICM42670P_ACCEL_CFG_2G_100HZ);
    
     icm42670p_write_reg(spi_handle_ICM42670P, ICM42670P_REG_GYRO_CONFIG1,ICM42670P_GYRO_CFG1_BW_16HZ);
    icm42670p_write_reg(spi_handle_ICM42670P, ICM42670P_REG_ACCEL_CONFIG1,ICM42670P_ACCEL_CFG1_BW_16HZ);

    ESP_LOGI(TAG, "ICM42670P inicializado");
    return ESP_OK;
}
// Leer datos del sensor
esp_err_t icm42670p_read_sensor_data(spi_device_handle_t spi, icm42670p_data_t *data)
{
    uint8_t buffer[14]; // temp(2) + accel(6) + gyro(6)

    // 🔧 leer desde 0x09 (TEMP_DATA1) hasta 0x16 (GYRO_Z0)
    esp_err_t ret = icm42670p_read_regs(spi, ICM42670P_REG_TEMP_DATA1, buffer, sizeof(buffer));
    if (ret != ESP_OK)
        return ret;

    int16_t raw_temp = (int16_t)((buffer[0] << 8) | buffer[1]);
    int16_t raw_accel_x = (int16_t)((buffer[2] << 8) | buffer[3]);
    int16_t raw_accel_y = (int16_t)((buffer[4] << 8) | buffer[5]);
    int16_t raw_accel_z = (int16_t)((buffer[6] << 8) | buffer[7]);
    int16_t raw_gyro_x = (int16_t)((buffer[8] << 8) | buffer[9]);
    int16_t raw_gyro_y = (int16_t)((buffer[10] << 8) | buffer[11]);
    int16_t raw_gyro_z = (int16_t)((buffer[12] << 8) | buffer[13]);

    // escalado
    data->temperature = ((float)raw_temp / ICM42670P_TEMP_SCALE) + ICM42670P_TEMP_OFFSET;
    data->accel[0] = (float)raw_accel_x / ICM42670P_ACCEL_SCALE - g_calib.accel[0];
    data->accel[1] = (float)raw_accel_y / ICM42670P_ACCEL_SCALE - g_calib.accel[1];
    data->accel[2] = (float)raw_accel_z / ICM42670P_ACCEL_SCALE - g_calib.accel[2];
    data->gyro[0] = (float)raw_gyro_x / ICM42670P_GYRO_SCALE - g_calib.gyro[0];
    data->gyro[1] = (float)raw_gyro_y / ICM42670P_GYRO_SCALE - g_calib.gyro[1];
    data->gyro[2] = (float)raw_gyro_z / ICM42670P_GYRO_SCALE - g_calib.gyro[2];

    return ESP_OK;
}
// Configuración inicial y calibración
esp_err_t icm42670p_setup(void)
{

    // Inicializar
    if (icm42670p_init(&spi_handle_ICM42670P) != ESP_OK)
    {
        ESP_LOGE(TAG, "Error iniciando ICM42670P");
        return ESP_FAIL;
    }

    // Calibrar
    icm42670p_calib_t calib;
    icm42670p_calibrate(spi_handle_ICM42670P, &calib, 1000); // 1000 muestras (~10 s)
    gpio_set_level(ICM42670P_SPI_CS, 1);                     // deshabilitar por defecto

    ESP_LOGI(TAG, "ICM42670P listo para adquisición");
    return ESP_OK;
}

// Función de prueba de funcionamiento
void app_ICM42670P(void)
{
    icm42670p_setup();
    icm42670p_data_t data;

    while (1)
    {
        if (icm42670p_read_sensor_data(spi_handle_ICM42670P, &data) == ESP_OK)
        {
            ESP_LOGI(TAG,
                     "T=%.2f °C | ACC[g]: X=%.3f Y=%.3f Z=%.3f | GYRO[dps]: X=%.3f Y=%.3f Z=%.3f",
                     data.temperature,
                     data.accel[0], data.accel[1], data.accel[2],
                     data.gyro[0], data.gyro[1], data.gyro[2]);
        }
        vTaskDelay(pdMS_TO_TICKS(100)); // 10 Hz logging (sensor = 100 Hz)
        break;                          // solo una iteración para prueba
    }
}
