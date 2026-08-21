// data_sensor.h

#ifndef SENSOR_DATA_H
#define SENSOR_DATA_H

#include <stdint.h>
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define JSON_BUFFER_SIZE 32768 // 32 KB
//#define GZIP_BUFFER_SIZE 65536 // 64 KB (para datos comprimidos)
//#define GZIP_BUFFER_SIZE (256 * 1024)
// Estructura para almacenar una muestra de datos del sensor
typedef struct
{
    float accX, accY, accZ;
    float gyroX, gyroY, gyroZ;
    float inclX, inclY;
} SensorSample;

// Tipo de señal entre tareas
typedef enum
{
    SIGNAL_ERROR_IN_COMPRESSION = 0,
    SIGNAL_NONE,
    SIGNAL_START_COLLECTION,
    SIGNAL_DATA_READY_FOR_COMPRESSION,
    SIGNAL_DATA_READY_FOR_SENDING,
    SIGNAL_HTTP_SEND,
    SIGNAL_SEND_OK,
    SIGNAL_SEND_FAIL
} data_signal_t;

typedef struct
{
    uint32_t current_index;
    uint32_t total_samples;
    float progress; // 0.0 - 1.0
    bool active;    // si está muestreando o no
} sample_status_t;

extern volatile sample_status_t sample_status;

// Variables globales accesibles
extern QueueHandle_t signal_queue;
extern TaskHandle_t gzip_task_handle;
extern QueueHandle_t signal_queue_task;
extern QueueHandle_t signal_queue_state;
extern QueueHandle_t mqtt_live_queue;
extern QueueHandle_t compression_signal_queue;
extern QueueHandle_t collection_signal_queue;

// Prototipos de funciones
void data_collection_task(void *arg);
void data_compression_task(void *arg);

size_t get_gzip_data_size();      // Obtiene el tamaño del último dato GZIP generado
size_t generate_json_gzip_data(); // Genera datos JSON y los comprime con GZIP
uint8_t *get_gzip_data_buffer();  // Obtiene el buffer de datos comprimidos

void sensor_memory_init(); // Inicializa memoria para buffers en PSRAM
#endif