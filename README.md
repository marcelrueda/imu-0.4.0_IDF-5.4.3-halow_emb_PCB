Proyecto: IMU
Version: 0.2.0
SDK: IDF 5.4.0
Autor: Carlos Andres Vargas C.
Correo: carlos_vargas_c@outlook.com

10/06/2025
Migracion al SDK 5.4.0
.
.
.
.

2/11/25 Version 1.31
Se agrega de metricas

- Intervercion en main.c, system_metrics\*, device_display.c

18/02/26
Migracion a IDF 5.4.3

- segmentacion de archivos por funciones en carpeta
- reconfiguracion Variable de entorno MMIOT_ROOT en forma local para usar halow en component

28/03/26

- Se documenta
- Se agrega pantalla de loading
  5% - 35%: Durante la inicialización de periféricos base (SPI, LEDs, RGB, ADC).
  40% - 60%: Durante la inicialización de la memoria NVS y las colas de FreeRTOS.
  70% - 100%: Durante la configuración de Red, IMU, Switches e inicio de MQTT.


22/06/26
- Mejoras para compatibilidad PSRAM octal en data_sensor.c
Funciones personalizadas de memoria para zlib: zlib_alloc y zlib_free que usan PSRAM primero y DRAM como fallback
Aumentado el buffer GZIP a 256KB para dar más espacio
Parámetros de compresión más conservadores: windowBits=9 y memLevel=5 como último recurso
zlib usa las funciones personalizadas en cada llamada a deflateInit2
Ahora zlib podrá usar PSRAM para sus estructuras internas, resolviendo el problema de falta de memoria contigua en DRAM.
-




Propuesta

Variables AccX, AccY, AccZ, Gx, Gy, Gz, Incx, Incy

30 x 60 =1800 muestras, se comprime en GZIP.

json
{
"timestamp": "2025-06-08T14:23:00Z",
"sensor_id": "mpu6500_azotea",
"sampling_rate": 30,
"duration": 60,
"units": {
"acceleration": "g",
"gyroscope": "deg/s",
"incremental": "units"
},
"data":
[
[0.17, 0.17, 0.98, -0.08, 0.08, 0.48, 0.0, 0.01 ],
[0.17, -0.0, 0.99, -0.07, 0.02, 0.54, 0.01, 0.02 ],
.......
]
}

imu_data tabla
id
deviceSerial
jsonName
created_at
update_at

Serial Halow Protipo 1 ???
Serial Halow Protipo 2 ID 0x306
