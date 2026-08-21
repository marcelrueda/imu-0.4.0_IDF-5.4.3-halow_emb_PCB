// system_metrics.c
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_task_wdt.h"

static const char *TAG = "METRICS";

// Devuelve un string con las métricas generales
void get_system_metrics_summary(char *buffer, size_t buffer_len)
{
    // --- 1. Uptime ---
    uint64_t uptime_ms = esp_timer_get_time() / 1000;
    uint32_t seconds = (uptime_ms / 1000) % 60;
    uint32_t minutes = (uptime_ms / (1000 * 60)) % 60;
    uint32_t hours = (uptime_ms / (1000 * 60 * 60)) % 24;
    uint32_t days = (uptime_ms / (1000 * 60 * 60 * 24));

    // --- 2. Uso real de CPU ---
    int cpu_usage_percent = 0;

#if (configGENERATE_RUN_TIME_STATS == 1)
    // buffer temporal para las estadísticas
    char *cpu_stats = malloc(2048);
    if (cpu_stats)
    {
        vTaskGetRunTimeStats(cpu_stats);

        // buscar líneas de IDLE0 e IDLE1
        char *idle0 = strstr(cpu_stats, "IDLE0");
        char *idle1 = strstr(cpu_stats, "IDLE1");

        float idle0_pct = 0, idle1_pct = 0;

        if (idle0)
            sscanf(idle0, "%*s %*u %f%%", &idle0_pct);
        if (idle1)
            sscanf(idle1, "%*s %*u %f%%", &idle1_pct);

        float avg_idle = (idle0_pct + idle1_pct) / 2.0f;
        cpu_usage_percent = (int)(100.0f - avg_idle);

        free(cpu_stats);
    }
#endif

    // --- 3. Memoria interna (SRAM) ---
    size_t free_heap = esp_get_free_heap_size();
    size_t total_heap = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
    size_t used_heap = total_heap - free_heap;

    // --- 4. PSRAM ---
    size_t total_psram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t used_psram = total_psram - free_psram;

    // --- 5. Armar resumen formateado ---
    snprintf(buffer, buffer_len,
             "Uptime: %02lu:%02lu:%02lu:%02lu\n" // dd:hh:mm:ss
             "CPU:   %d%% uso\n"                 // uso CPU estimado
             "RAM:   %u/%u KB\n"                 // usada/total
             "PSRAM: %u/%u KB",                  // usada/total
             days, hours, minutes, seconds,
             cpu_usage_percent,
             (unsigned int)(used_heap / 1024), (unsigned int)(total_heap / 1024),
             (unsigned int)(used_psram / 1024), (unsigned int)(total_psram / 1024));
    buffer[buffer_len - 1] = '\0'; // Garantía de terminador
}

// Tarea de monitoreo de métricas del sistema
static void system_metrics_task(void *pvParameters)
{
    while (1)
    {
        // ----- Heap info -----
        size_t free_heap = esp_get_free_heap_size();                    // total free heap
        size_t min_heap = esp_get_minimum_free_heap_size();             // minimum ever free heap
        size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM); // free PSRAM

        ESP_LOGI(TAG, "Heap interno libre: %u bytes (mínimo: %u)", free_heap, min_heap); // Internal heap free (SRAM)
        ESP_LOGI(TAG, "Heap PSRAM libre: %u bytes", free_psram);                         // PSRAM free

        // ----- Task info -----
#if (configUSE_TRACE_FACILITY == 1) && (configUSE_STATS_FORMATTING_FUNCTIONS == 1)
        char *task_stats = malloc(2048);
        if (task_stats)
        {
            vTaskList(task_stats);
            ESP_LOGI(TAG, "\nTareas activas:\n%s", task_stats);
            free(task_stats);
        }
#endif

        // ----- CPU load estimado -----
#if (configGENERATE_RUN_TIME_STATS == 1)
        char *cpu_stats = malloc(2048);
        if (cpu_stats)
        {
            vTaskGetRunTimeStats(cpu_stats);
            ESP_LOGI(TAG, "\nUso de CPU por tarea:\n%s", cpu_stats);
            free(cpu_stats);
        }
#endif

        vTaskDelay(pdMS_TO_TICKS(5000)); // Cada 5s
    }
}

// Función para iniciar la tarea de métricas del sistema
BaseType_t start_system_metrics_task(void)
{
    return xTaskCreatePinnedToCore(system_metrics_task, "system_metrics_task", 4096, NULL, 2, NULL, 0);
}

/*
LOG DE MÉTRICAS DEL SISTEMA

Heap interno libre: 8291936 bytes (mínimo: 7058148)

Esto se refiere a la RAM interna (SRAM) del ESP32, es decir, la memoria de alta velocidad dentro del chip, usada para:
Pilas de tareas (stack)
Colas, semáforos, buffers, estructuras del RTOS
Variables globales dinámicas (malloc, pvPortMalloc, etc.)
Librerías del sistema, Wi-Fi, MQTT, etc.


| Campo                   | Valor                        | Significado                                                                                                                            |
| ----------------------- | ---------------------------- | -------------------------------------------------------------------------------------------------------------------------------------- |
| **Heap interno libre**  | `8,291,936 bytes` (~8.29 MB) | Cantidad actual de memoria interna libre y disponible para asignaciones dinámicas.                                                     |
| **(mínimo: 7,058,148)** | ~7.05 MB                     | El punto más bajo de heap interno que ha tenido el sistema desde que arrancó. Es decir, el momento en que más RAM interna se ha usado. |



Heap PSRAM libre: 8170204 bytes
La PSRAM (Pseudo-Static RAM) es memoria externa al chip, conectada por QSPI.
Más lenta, pero mucho más grande. Ideal para datos grandes, buffers de compresión, imágenes, archivos, etc.

| Campo                | Valor                        | Significado                          |
| -------------------- | ---------------------------- | ------------------------------------ |
| **Heap PSRAM libre** | `8,170,204 bytes` (~8.17 MB) | Cantidad de PSRAM actualmente libre. |



“Tareas activas” (vTaskList)
system_metrics_     X       2       1876    15
IDLE1               R       0       688     6
display_task        B       3       3848    19

| Columna                      | Ejemplo            | Significado                                                                   | Fuente / API                                      |
| ---------------------------- | ------------------ | ----------------------------------------------------------------------------- | ------------------------------------------------- |
| **Nombre de tarea**          | `display_task`     | Nombre que diste al crearla con `xTaskCreate()` o `xTaskCreatePinnedToCore()` | Asignado por el usuario                           |
| **Estado (Status)**          | `R`, `B`, `S`, `X` | Estado actual de la tarea                                                     | Interno de FreeRTOS                               |
| **Prioridad (Prio)**         | `5`, `3`, `24`     | Prioridad de ejecución asignada a la tarea                                    | `uxPriority`                                      |
| **Stack libre (Stack Free)** | `3848`, `688`      | Cantidad de *bytes libres* en la pila de esa tarea (aprox.)                   | `uxTaskGetStackHighWaterMark()`                   |
| **Core / ID TCB**            | `19`, `6`, `1`     | Número interno del TCB o índice en la lista de tareas (depende del puerto)    | No siempre es core_id, aunque suele correlacionar |

Tabla de estados (Status):
R	Running	                       Actualmente ejecutándose en un core
B	Blocked	                       Esperando un delay, semáforo o cola
R+	Ready	                       Lista para ejecutar, esperando turno
S	Suspended	                   Suspendida manualmente
D	Deleted	                       Eliminada, pero aún no liberada (raro)
X	En espera o inactiva (Idle)    Usualmente una tarea que corre solo por instantes o finalizó

Ejemplo:
display_task    B       3       3848    19
display_task está bloqueada, tiene prioridad 3, y su pila aún tiene 3.8 KB libres de los 6 KB que le diste.


“Uso de CPU por tarea” (vTaskGetRunTimeStats)

display_task    37315558        2%
data_compressio 46748013        2%
IDLE1           1624858216      98%
| Columna                                    | Ejemplo        | Significado                                                                                                             | Fuente / API                           |
| ------------------------------------------ | -------------- | ----------------------------------------------------------------------------------------------------------------------- | -------------------------------------- |
| **Nombre de tarea**                        | `display_task` | Igual que antes                                                                                                         | Nombre asignado                        |
| **Ticks acumulados / tiempo de ejecución** | `37315558`     | Número total de “ticks” o *unidades de tiempo de CPU* que la tarea ha consumido desde el arranque (contador de 32 bits) | `ulRunTimeCounter` interno de FreeRTOS |
| **% CPU estimado**                         | `2%`           | Porcentaje de tiempo que esa tarea ha ocupado el procesador desde que empezó el muestreo                                | Calculado por `vTaskGetRunTimeStats()` |

Ejemplo:
data_compressio 46748013                2%
La tarea de compresión ha usado 46,7 millones de “ticks” de CPU (~2% del total).
IDLE1           1624858216              98%
El core 1 ha estado ocioso el 98% del tiempo; solo un 2% ocupado.


*/