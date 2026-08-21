// imu.c

#include "imu/imu.h"
#include "bus/bus_spi.h"      // Bus SPI
#include "device_icm42670p.h" // Sensor ICM-42670-P
#include "device_SCL3400.h"   // Sensor SCL3400
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "IMU";

// Estructura para capturar y almacenar los datos de la IMU
data_imu_t generate_imu_sample(void)
{
    data_imu_t imu_data = {0}; // Inicializa todo en 0

    icm42670p_data_t icm_data; // estructura del driver ICM
    if (icm42670p_read_sensor_data(icm_get_handle(), &icm_data) == ESP_OK)
    {
        imu_data.accX = icm_data.accel[0];
        imu_data.accY = icm_data.accel[1];
        imu_data.accZ = icm_data.accel[2];

        imu_data.gyroX = icm_data.gyro[0];
        imu_data.gyroY = icm_data.gyro[1];
        imu_data.gyroZ = icm_data.gyro[2];

        imu_data.temp_icm = icm_data.temperature;
    }

    // Leer datos del SCL3400
    float x_scl = 0, y_scl = 0, temp_scl = 0;
    if (scl3400_read_all_noCRC(SCL3400_get_handle(), &x_scl, &y_scl, &temp_scl))
    {
        imu_data.inclX = scl3400_calculate_angle(x_scl);
        imu_data.inclY = scl3400_calculate_angle(y_scl);
        imu_data.temp_scl = temp_scl;

        // float inclination_deg = scl3400_get_inclination_deg(x_scl, y_scl);
        // ESP_LOGI(TAG, "Inclinación total = %.2f°", inclination_deg);
    }

    return imu_data;
}

// Función para inicializar los sensores IMU
void app_IMU(void)
{
    ESP_LOGI(TAG, "Inicializando IMU");
    app_ICM42670P(); // Configurando e inicializando sensor ICM-42670-P
    app_SCL3400();   // Configurando e inicializando sensor SCL3400

    ESP_LOGI(TAG, "Sensores IMU inicializados correctamente.");

    while (1)
    {
        data_imu_t imu_data = generate_imu_sample();

        ESP_LOGI(TAG, "IMU DATA:");
        ESP_LOGI(TAG, "  ACC:  X=%.2f g, Y=%.2f g, Z=%.2f g", imu_data.accX, imu_data.accY, imu_data.accZ);
        ESP_LOGI(TAG, "  GYRO: X=%.2f dps, Y=%.2f dps, Z=%.2f dps", imu_data.gyroX, imu_data.gyroY, imu_data.gyroZ);
        ESP_LOGI(TAG, "  INCL: X=%.2f°, Y=%.2f°", imu_data.inclX, imu_data.inclY);
        ESP_LOGI(TAG, "  TEMP: ICM=%.2f °C, SCL=%.2f °C", imu_data.temp_icm, imu_data.temp_scl);

        vTaskDelay(pdMS_TO_TICKS(50));

        break; // Eliminar esta línea si se desea que el bucle sea infinito
    }
}
