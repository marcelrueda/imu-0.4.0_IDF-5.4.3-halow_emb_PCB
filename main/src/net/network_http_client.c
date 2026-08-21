// network_http_client_optimized.c

#include "imu/data_sensor.h"
#include "common/utils.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_timer.h"

static const char *TAG = "HTTP_CLIENT";

#include "net/network_config.h"

// ---------------------------------------------------------
// La configuración de URL y tiempos se toma de network_config.h
// ---------------------------------------------------------

QueueHandle_t http_signal_queue = NULL;

static esp_http_client_handle_t create_http_client(const char *url)
{
    if (!url)
        return NULL;

    char full_url[MAX_URL_LENGTH];
    snprintf(full_url, MAX_URL_LENGTH, "%s%s", url, HTTP_UPLOAD_PATH);

    esp_http_client_config_t config = {
        .url = full_url,               // URL completa
        .event_handler = NULL,         // Opcional si solo quieres logs en tarea
        .timeout_ms = HTTP_TIMEOUT_MS, // Tiempo de espera

    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client)
    {
        ESP_LOGE(TAG, "No se pudo inicializar el cliente HTTP");
        return NULL;
    }

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/gzip");
    esp_http_client_set_header(client, "Content-Encoding", "gzip");
    esp_http_client_set_header(client, "Connection", "close");
    esp_http_client_set_header(client, "x-device-id", device_id);

    return client;
}

static bool send_gzip_data(const char *url, size_t data_size)
{
    if (!url || data_size == 0)
    {
        ESP_LOGE(TAG, "Datos vacíos o URL nula");
        return false;
    }

    int64_t start_time = esp_timer_get_time();

    esp_http_client_handle_t client = create_http_client(url);
    if (!client)
        return false;

    esp_http_client_set_post_field(client, (const char *)get_gzip_data_buffer(), data_size);
    esp_err_t err = esp_http_client_perform(client);
    int64_t end_time = esp_timer_get_time();
    float duration_ms = (end_time - start_time) / 1000.0f;

    bool success = false;
    if (err == ESP_OK)
    {
        int status = esp_http_client_get_status_code(client);
        if (status >= 200 && status < 300)
        {
            ESP_LOGI(TAG, "HTTP POST OK (%d), duración %.2f ms", status, duration_ms);
            success = true;
        }
        else
        {
            ESP_LOGW(TAG, "HTTP POST fallo %d, duración %.2f ms", status, duration_ms);
        }
    }
    else
    {
        ESP_LOGE(TAG, "Error en HTTP: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return success;
}

void http_send_task(void *pvParameters)
{
    data_signal_t sig;
    while (1)
    {
        if (xQueueReceive(http_signal_queue, &sig, portMAX_DELAY) && sig == SIGNAL_HTTP_SEND)
        {
            size_t gzip_size = get_gzip_data_size();
            if (gzip_size == 0)
            {
                ESP_LOGW(TAG, "No hay datos para enviar");
                continue;
            }

            set_status_mode_data_sending(); // LED amarillo

            bool sent = false;
            for (int attempt = 1; attempt <= MAX_RETRIES && !sent; attempt++)
            {
                ESP_LOGI(TAG, "HTTP_TASK: intento %d/%d", attempt, MAX_RETRIES);
                sent = send_gzip_data(HTTP_SERVER_URL, gzip_size);

                if (!sent)
                {
                    ESP_LOGW(TAG, "Intento %d fallido", attempt);
                    vTaskDelay(pdMS_TO_TICKS(50)); // cede CPU antes de reintentar
                }
            }

            if (sent)
            {
                ESP_LOGI(TAG, "HTTP_TASK: envío completado");
                if (signal_queue_state)
                {
                    data_signal_t ok_sig = SIGNAL_SEND_OK;
                    xQueueSend(signal_queue_state, &ok_sig, 0);
                }
            }
            else
            {
                ESP_LOGE(TAG, "HTTP_TASK: envío fallido tras %d intentos", MAX_RETRIES);
            }

            turn_off_status_led(); // Apaga LED
        }
    }
}
