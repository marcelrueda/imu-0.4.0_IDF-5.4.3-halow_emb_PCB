#ifndef NETWORK_HTTP_CLIENT_H
#define NETWORK_HTTP_CLIENT_H

#include "esp_err.h"
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#ifdef __cplusplus
extern "C"
{
#endif
    extern QueueHandle_t http_signal_queue;

    void http_send_task(void *pv); // Tarea para manejar el envío HTTP sin bloquear la FSM

#ifdef __cplusplus
}
#endif

#endif // NETWORK_HTTP_CLIENT_H