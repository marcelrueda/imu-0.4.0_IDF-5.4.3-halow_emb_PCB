// data_sensor.c

#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_random.h"
#include "cJSON.h"
#include "zlib.h"
#include "esp_heap_caps.h" // <--- Necesario para usar PSRAM

#include "net/network_mqtt.h"
#include "net/network_http_client.h"
#include "imu/data_sensor.h"
#include "imu/imu.h"
#include "imu/imu_tools.h"
#include "common/utils.h"

#define SAMPLE_COUNT 1800             // 30 muestras/s * 60 s
#define SAMPLE_INTERVAL_MS 33         // ~30Hz (1000ms / 33)
#define GZIP_BUFFER_SIZE (1024 * 256) // 256KB para compresión

static const char *TAG = "SENSOR_DATA";
volatile data_imu_t last_sample;

// ----------------- Buffers dobles (PSRAM) -----------------
static data_imu_t *bufferA = NULL;
static data_imu_t *bufferB = NULL;
static data_imu_t *active_buffer = NULL; // El que se está llenando
static data_imu_t *ready_buffer = NULL;  // El que espera compresión
static bool buffer_ready = false;

static size_t current_index = 0;
static size_t last_gzip_size = 0;

// ----------------- Buffer para GZIP (en PSRAM) -----------------
static uint8_t *gzip_data = NULL;

// ----------------- Colas y tareas -----------------
QueueHandle_t mqtt_live_queue = NULL;          // Cola para muestras en vivo
QueueHandle_t signal_queue_task = NULL;        // Comunicación entre tareas
QueueHandle_t signal_queue_state = NULL;       // Comunicación con máquina de estados
QueueHandle_t compression_signal_queue = NULL; //   Cola para señales de compresión
QueueHandle_t collection_signal_queue = NULL;  //   Cola para señales de colección

TaskHandle_t gzip_task_handle = NULL; // Handle de compresión (si se necesita cancelar)

volatile sample_status_t sample_status = {
    .current_index = 0,
    .total_samples = SAMPLE_COUNT,
    .progress = 0.0f,
    .active = false};

// ---------------------- Funciones alloc PSRAM ----------------------

static void *psram_malloc(size_t sz)
{
    void *p = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
    if (!p)
    {
        ESP_LOGW(TAG, "PSRAM agotada, usando DRAM (%d bytes)", (int)sz);
        p = heap_caps_malloc(sz, MALLOC_CAP_8BIT);
    }
    return p;
}

static void psram_free(void *ptr)
{
    heap_caps_free(ptr);
}

// ---------------------- Funciones de memoria personalizadas para zlib ----------------------

// zlib necesita que las funciones de memoria devuelvan punteros alineados
// y que manejen correctamente la memoria en PSRAM
static voidpf zlib_alloc(voidpf opaque, uInt items, uInt size)
{
    (void)opaque; // No usado
    size_t total_size = (size_t)items * (size_t)size;

    // Intentar primero en PSRAM
    void *ptr = heap_caps_malloc(total_size, MALLOC_CAP_SPIRAM);
    if (!ptr)
    {
        ESP_LOGW(TAG, "zlib: No hay PSRAM para %d bytes, usando DRAM", (int)total_size);
        ptr = heap_caps_malloc(total_size, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    }

    if (!ptr)
    {
        ESP_LOGE(TAG, "zlib: Falló asignación de %d bytes", (int)total_size);
        return Z_NULL;
    }

    // ESP_LOGD(TAG, "zlib alloc: %d bytes en %p", (int)total_size, ptr);
    return ptr;
}

static void zlib_free(voidpf opaque, voidpf address)
{
    (void)opaque; // No usado
    if (address)
    {
        // ESP_LOGD(TAG, "zlib free: %p", address);
        heap_caps_free(address);
    }
}

// ---------------------- Inicialización de memoria ----------------------

void sensor_memory_init()
{
    bufferA = (data_imu_t *)psram_malloc(SAMPLE_COUNT * sizeof(data_imu_t));
    bufferB = (data_imu_t *)psram_malloc(SAMPLE_COUNT * sizeof(data_imu_t));

    // Buffer GZIP en PSRAM (con funciones personalizadas de zlib)
    gzip_data = (uint8_t *)psram_malloc(GZIP_BUFFER_SIZE);

    if (!bufferA || !bufferB)
    {
        ESP_LOGE(TAG, "Error: no se pudo asignar memoria para buffers en PSRAM/DRAM");
    }

    if (!gzip_data)
    {
        ESP_LOGE(TAG, "Error: no se pudo asignar memoria para GZIP en PSRAM");
        // Intentar con un buffer más pequeño
        gzip_data = (uint8_t *)psram_malloc((size_t)(GZIP_BUFFER_SIZE / 2));
        if (gzip_data)
        {
            ESP_LOGI(TAG, "Buffer GZIP reducido a %d bytes", (int)(GZIP_BUFFER_SIZE / 2));
        }
    }
    else
    {
        ESP_LOGI(TAG, "Buffers A=%p B=%p gzip=%p (PSRAM)", (void *)bufferA, (void *)bufferB, (void *)gzip_data);
    }

    // Configurar zlib para usar nuestras funciones de memoria personalizadas
    // Esto permite que zlib use PSRAM
    z_stream zlib_stream = {0};
    zlib_stream.zalloc = zlib_alloc;
    zlib_stream.zfree = zlib_free;

// Aplicar la configuración global para zlib
// Nota: zlib no tiene una función global para cambiar allocators,
// pero podemos pasar las funciones en cada llamada a deflateInit2
// o usar la función de configuración global si está disponible

// Para zlib en ESP-IDF, podemos usar la función de configuración
// si está disponible en la versión de zlib
#ifdef ZLIB_VERSION
    // Algunas versiones de zlib permiten configurar globalmente
    // Pero mejor usamos las funciones en cada llamada
#endif

    active_buffer = bufferA;
    ready_buffer = bufferB;
    current_index = 0;
    buffer_ready = false;

    // Forzar a cJSON a usar PSRAM también
    cJSON_Hooks hooks = {
        .malloc_fn = psram_malloc,
        .free_fn = psram_free};
    cJSON_InitHooks(&hooks);
}

// ---------------------- Funciones auxiliares ----------------------

void add_float_to_array(cJSON *array, float value, int precision)
{
    char buf[16];
    snprintf(buf, sizeof(buf), precision == 2 ? "%.2f" : "%.3f", (double)value);
    cJSON_AddItemToArray(array, cJSON_CreateString(buf));
}

static void swap_buffers()
{
    data_imu_t *tmp = active_buffer;
    active_buffer = ready_buffer;
    ready_buffer = tmp;
    current_index = 0;
    buffer_ready = true;
}

// ---------------------- Generación JSON+GZIP ----------------------

size_t generate_json_gzip_from_buffer(data_imu_t *buf)
{
    ESP_LOGI(TAG, "Generando JSON + GZIP...");

    // Verificar memoria disponible antes de comprimir
    ESP_LOGI(TAG, "Heap libre antes de comprimir: %d bytes", (int)esp_get_free_heap_size());
    ESP_LOGI(TAG, "PSRAM libre antes de comprimir: %d bytes", (int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    ESP_LOGI(TAG, "Mayor bloque contiguo en DRAM: %d bytes", (int)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL));

    cJSON *root = cJSON_CreateObject();
    if (!root)
    {
        ESP_LOGE(TAG, "Falló creación del JSON root");
        return 0;
    }

    time_t now;
    struct tm timeinfo;
    char timestamp_str[64];
    time(&now);
    localtime_r(&now, &timeinfo);
    strftime(timestamp_str, sizeof(timestamp_str), "%Y-%m-%d %H:%M:%S %Z", &timeinfo);

    cJSON_AddStringToObject(root, "timestamp", timestamp_str);
    cJSON_AddStringToObject(root, "sensor_id", device_id);
    cJSON_AddNumberToObject(root, "sampling_rate", (double)SAMPLE_COUNT);
    cJSON_AddNumberToObject(root, "duration", 60.0);

    cJSON *units = cJSON_CreateObject();
    cJSON_AddStringToObject(units, "acceleration", "g");
    cJSON_AddStringToObject(units, "gyroscope", "deg/s");
    cJSON_AddStringToObject(units, "incremental", "units");
    cJSON_AddItemToObject(root, "units", units);

    cJSON *data_array = cJSON_CreateArray();
    for (int i = 0; i < SAMPLE_COUNT; i++)
    {
        data_imu_t imu_data = buf[i];
        cJSON *sample_array = cJSON_CreateArray();

        add_float_to_array(sample_array, imu_data.accX, 3);
        add_float_to_array(sample_array, imu_data.accY, 3);
        add_float_to_array(sample_array, imu_data.accZ, 3);
        add_float_to_array(sample_array, imu_data.gyroX, 3);
        add_float_to_array(sample_array, imu_data.gyroY, 3);
        add_float_to_array(sample_array, imu_data.gyroZ, 3);
        add_float_to_array(sample_array, imu_data.inclX, 2);
        add_float_to_array(sample_array, imu_data.inclY, 2);

        cJSON_AddItemToArray(data_array, sample_array);
    }
    cJSON_AddItemToObject(root, "data", data_array);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!json_str)
    {
        ESP_LOGE(TAG, "Falló generación de string JSON");
        return 0;
    }

    ESP_LOGI(TAG, "JSON generado: %d bytes", (int)strlen(json_str));

    // Verificar que el buffer GZIP existe y tiene tamaño suficiente
    if (!gzip_data)
    {
        ESP_LOGE(TAG, "Buffer GZIP no inicializado");
        free(json_str);
        return 0;
    }

    // Compresión GZIP con zlib usando funciones de memoria personalizadas
    z_stream defstream = {0};
    defstream.zalloc = zlib_alloc; // Usar PSRAM para la memoria interna de zlib
    defstream.zfree = zlib_free;
    defstream.opaque = Z_NULL;
    defstream.avail_in = (uInt)strlen(json_str);
    defstream.next_in = (Bytef *)json_str;
    defstream.avail_out = (uInt)GZIP_BUFFER_SIZE;
    defstream.next_out = (Bytef *)gzip_data;

    // Configurar zlib para usar menos memoria
    int windowBits = 15; // Ventana estándar (32KB)
    int memLevel = 8;    // Nivel de memoria (máximo 9, usar 8 para ahorrar)

    ESP_LOGI(TAG, "Intentando compresión con windowBits=%d, memLevel=%d", windowBits, memLevel);

    // Intentar compresión GZIP estándar
    int ret = deflateInit2(&defstream, Z_BEST_SPEED, Z_DEFLATED,
                           windowBits + 16, memLevel, Z_DEFAULT_STRATEGY);

    if (ret != Z_OK)
    {
        ESP_LOGE(TAG, "Error iniciando deflate GZIP: %d", (int)ret);
        ESP_LOGW(TAG, "Reduciendo requisitos de memoria...");

        // Liberar recursos del intento anterior
        deflateEnd(&defstream);

        // Reintentar con parámetros de menor memoria
        windowBits = 12; // Ventana reducida (4KB)
        memLevel = 6;    // Menos memoria

        defstream.zalloc = zlib_alloc;
        defstream.zfree = zlib_free;
        defstream.opaque = Z_NULL;
        defstream.avail_in = (uInt)strlen(json_str);
        defstream.next_in = (Bytef *)json_str;
        defstream.avail_out = (uInt)GZIP_BUFFER_SIZE;
        defstream.next_out = (Bytef *)gzip_data;

        ret = deflateInit2(&defstream, Z_BEST_SPEED, Z_DEFLATED,
                           windowBits + 16, memLevel, Z_DEFAULT_STRATEGY);

        if (ret != Z_OK)
        {
            ESP_LOGE(TAG, "Fallo con parámetros reducidos: %d", (int)ret);

            // Último intento: ZLIB estándar sin encabezado y parámetros mínimos
            ESP_LOGW(TAG, "Intentando ZLIB estándar (sin encabezado) con parámetros mínimos...");

            windowBits = 9; // Ventana mínima (512 bytes)
            memLevel = 5;   // Memoria mínima

            defstream.zalloc = zlib_alloc;
            defstream.zfree = zlib_free;
            defstream.opaque = Z_NULL;
            defstream.avail_in = (uInt)strlen(json_str);
            defstream.next_in = (Bytef *)json_str;
            defstream.avail_out = (uInt)GZIP_BUFFER_SIZE;
            defstream.next_out = (Bytef *)gzip_data;

            ret = deflateInit2(&defstream, Z_BEST_SPEED, Z_DEFLATED,
                               windowBits, memLevel, Z_DEFAULT_STRATEGY);

            if (ret != Z_OK)
            {
                ESP_LOGE(TAG, "Fallo también con ZLIB estándar: %d", (int)ret);
                ESP_LOGE(TAG, "Liberando memoria y abortando compresión");
                free(json_str);
                return 0;
            }
            ESP_LOGI(TAG, "Usando ZLIB estándar (sin encabezado) con parámetros mínimos");
        }
        else
        {
            ESP_LOGI(TAG, "Usando GZIP con parámetros reducidos");
        }
    }
    else
    {
        ESP_LOGI(TAG, "Usando GZIP con parámetros estándar");
    }

    // Ejecutar la compresión
    ret = deflate(&defstream, Z_FINISH);
    if (ret != Z_STREAM_END)
    {
        ESP_LOGE(TAG, "Error en compresión: %d", (int)ret);
        deflateEnd(&defstream);
        free(json_str);
        return 0;
    }

    // Finalizar y obtener tamaño comprimido
    deflateEnd(&defstream);
    size_t compressed_size = (size_t)(GZIP_BUFFER_SIZE - defstream.avail_out);
    last_gzip_size = compressed_size;

    ESP_LOGI(TAG, "Compresión exitosa: JSON=%d bytes, GZIP=%d bytes (ratio: %.1f%%)",
             (int)strlen(json_str), (int)compressed_size,
             (double)(((float)compressed_size / (float)strlen(json_str)) * 100.0f));

    free(json_str);
    return compressed_size;
}

// ---------------------- Tareas ------------------------------------

void data_collection_task(void *arg)
{
    TickType_t xLastWakeTime;
    const TickType_t xFrequency = pdMS_TO_TICKS(SAMPLE_INTERVAL_MS);

    // Inicializar la marca de tiempo para la primera iteración.
    xLastWakeTime = xTaskGetTickCount();

    while (1)
    {
        current_index = 0;
        buffer_ready = false;

        while (current_index < SAMPLE_COUNT)
        {
            active_buffer[current_index++] = generate_imu_sample();
            last_sample = active_buffer[current_index - 1];
            check_for_seismic_event(last_sample);

            // Enviar muestra en vivo cada 10 muestras
            if (current_index % 10 == 0)
            {
                xQueueSend(mqtt_live_queue, &last_sample, 0);
            }

            // Actualizar estado para display
            sample_status.active = true;
            sample_status.current_index = current_index;
            sample_status.progress = (float)current_index / (float)SAMPLE_COUNT;

            // Refresco cada 300 muestras solo para log
            if (current_index % 300 == 0)
            {
                ESP_LOGI(TAG, "Progreso: %d/%d muestras", (int)current_index, (int)SAMPLE_COUNT);
            }

            // A diferencia de vTaskDelay, esta función "duerme" la tarea
            // hasta el siguiente "pulso" de tiempo, garantizando un ritmo
            // más constante, incluso si el muestreo y la lógica toman
            // más de lo esperado.
            vTaskDelayUntil(&xLastWakeTime, xFrequency);
        }
        // Cuando termina el ciclo de muestreo
        sample_status.active = false;
        sample_status.progress = 1.0f;

        swap_buffers();
        ESP_LOGI(TAG, "Buffer lleno. Enviando señal a compresión.");

        data_signal_t done = SIGNAL_DATA_READY_FOR_COMPRESSION;
        BaseType_t sent = xQueueSend(compression_signal_queue, &done, pdMS_TO_TICKS(200));
        if (sent != pdPASS)
        {
            ESP_LOGE(TAG, "Fallo al enviar señal a compression (cola llena). code=%d", (int)sent);
        }
        else
        {
            ESP_LOGI(TAG, "Señal enviada a compression (ok).");
        }
    }
}

void data_compression_task(void *arg)
{
    data_signal_t sig;

    while (1)
    {
        if (xQueueReceive(compression_signal_queue, &sig, portMAX_DELAY))
        {
            if (sig == SIGNAL_DATA_READY_FOR_COMPRESSION && buffer_ready)
            {
                ESP_LOGI(TAG, "Iniciando compresión de datos...");

                // Verificar que el buffer esté disponible
                if (!ready_buffer)
                {
                    ESP_LOGE(TAG, "Buffer ready es NULL");
                    buffer_ready = false;
                    continue;
                }

                // Aumentar prioridad temporalmente para la compresión
                UBaseType_t original_priority = uxTaskPriorityGet(NULL);
                vTaskPrioritySet(NULL, configMAX_PRIORITIES - 1);

                size_t gzip_size = generate_json_gzip_from_buffer(ready_buffer);

                // Restaurar prioridad
                vTaskPrioritySet(NULL, original_priority);

                if (gzip_size > 0)
                {
                    ESP_LOGI(TAG, "Compresión OK (%lu bytes)", (unsigned long)gzip_size);
                    data_signal_t ready = SIGNAL_HTTP_SEND;
                    if (xQueueSend(http_signal_queue, &ready, portMAX_DELAY) != pdPASS)
                    {
                        ESP_LOGE(TAG, "Cola de estado llena. Señal perdida (%d)", (int)ready);
                    }
                    ESP_LOGI(TAG, "Compresión terminada, notificando a FSM...");
                }
                else
                {
                    ESP_LOGE(TAG, "Fallo en compresión. No se enviará señal.");
                }

                buffer_ready = false;
            }
        }
    }
}

// ---------------------- Getters ----------------------

uint8_t *get_gzip_data_buffer()
{
    return gzip_data;
}

size_t get_gzip_data_size()
{
    return last_gzip_size;
}