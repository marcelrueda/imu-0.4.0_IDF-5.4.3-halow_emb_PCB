/**
 * @file system_metrics.h
 * @brief Monitoreo de métricas del sistema (SoC y RTOS)
 *
 * Este módulo crea una tarea periódica que muestra información sobre:
 *  - Uso de memoria heap y PSRAM
 *  - Estado de tareas del RTOS
 *  - Estadísticas de uso de CPU por tarea (si está habilitado en sdkconfig)
 *
 * Requisitos de configuración en sdkconfig:
 *  - CONFIG_FREERTOS_USE_TRACE_FACILITY=y
 *  - CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS=y
 *  - CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS=y (opcional, para CPU load)
 *
 * Ejemplo de uso:
 *  ```c
 *  #include "system_metrics.h"
 *
 *  void app_main(void) {
 *      start_system_metrics_task();
 *  }
 *  ```
 */

#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Crea e inicia la tarea de monitoreo de métricas del sistema.
     *
     * La tarea se ejecuta en un núcleo fijo (core 0) con prioridad 2
     * y realiza un muestreo cada 5 segundos.
     *
     * @return pdPASS si la tarea se creó correctamente, o pdFAIL en caso de error.
     */
    BaseType_t start_system_metrics_task(void);

    void get_system_metrics_summary(char *buffer, size_t buffer_len);

#ifdef __cplusplus
}
#endif
