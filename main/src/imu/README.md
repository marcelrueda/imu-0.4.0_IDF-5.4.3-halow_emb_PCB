# Directorio: imu

## Autoría
- **Nombre:** Carlos Andres Vargas Calderon
- **Correo:** carlos_vargas_c@outlook.com
- **Profesión:** Ing Electronico
- **Empresa:** Lycans Electronics - 2026

## Descripción de Archivos
Controladores para sensores de movimiento e inclinación (IMU).

- **imu.c / .h:** Abstracción principal para la gestión de los sensores inerciales.
- **data_sensor.c / .h:** Lógica de procesamiento y almacenamiento temporal de las lecturas de los sensores.
- **device_SCL3400.c / .h:** Controlador específico para el acelerómetro/inclinómetro digital Murata SCL3400.
- **device_icm42670p.c / .h:** Controlador para el sensor IMU de 6 ejes TDK InvenSense ICM-42670-P.
- **device_mpu6500.c / .h:** Controlador para el sensor IMU de 6 ejes MPU6500.
- **imu_tools.c / .h:** Funciones matemáticas y herramientas de calibración para los sensores IMU.
