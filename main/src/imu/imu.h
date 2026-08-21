// imu.h
#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

    //  Estructura para almacenar los datos de la IMU
    typedef struct
    {
        float accX;     // Aceleración en g en eje X (de ICM-42670-P)
        float accY;     // Aceleración en g en eje Y (de ICM-42670-P)
        float accZ;     // Aceleración en g en eje Z (de ICM-42670-P)
        float gyroX;    // Velocidad angular en grados por segundo en eje X (de ICM-42670-P)
        float gyroY;    // Velocidad angular en grados por segundo en eje Y (de ICM-42670-P)
        float gyroZ;    // Velocidad angular en grados por segundo en eje Z (de ICM-42670-P)
        float inclX;    // Ángulo en grados en eje X (de SCL3400)
        float inclY;    // Ángulo en grados en eje Y (de SCL3400)
        float temp_icm; // Temperatura del ICM-42670-P
        float temp_scl; // Temperatura del SCL3400
    } data_imu_t;

    data_imu_t generate_imu_sample(void);
    void app_IMU(void);

#ifdef __cplusplus
}
#endif