// data_sensor.c

#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "net/network_mqtt.h"

#include "imu/data_sensor.h"
#include "imu/imu.h"
#include "imu/imu_tools.h"
#include "common/utils.h"

#define SAMPLE_INTERVAL_MS 33 // ~30Hz (1000ms / 33)

static const char *TAG = "SENSOR_DATA";
volatile data_imu_t last_sample;

// ----------------- Colas y tareas -----------------
QueueHandle_t mqtt_live_queue = NULL;         // Cola para muestras en vivo
QueueHandle_t signal_queue_task = NULL;       // Comunicación entre tareas
QueueHandle_t signal_queue_state = NULL;      // Comunicación con máquina de estados
QueueHandle_t collection_signal_queue = NULL; // Cola para señales de colección

volatile sample_status_t sample_status = {
    .current_index = 0,
    .total_samples = 0,
    .progress = 0.0f,
    .active = false};

// ---------------------- Tareas ------------------------------------

void data_collection_task(void *arg)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(SAMPLE_INTERVAL_MS);

    while (1)
    {
        data_imu_t s = generate_imu_sample();
        last_sample = s;
        check_for_seismic_event(s);

        xQueueSend(mqtt_live_queue, &s, 0);

        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}