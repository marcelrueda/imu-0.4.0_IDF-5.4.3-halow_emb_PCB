// network_mqtt.c

#include "net/network_mqtt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "common/utils.h"

static const char *TAG = "MQTT";

static bool mqtt_connected = false;
esp_mqtt_client_handle_t mqtt_client_global = NULL;
char topic[32] = {0}; // topic mac/imu-live

void get_mqtt_topic(char *topic_buf, size_t buf_len)
{
    char mac_str[13];                                     // 12 + null terminator
    get_device_id(mac_str, sizeof(mac_str));              // Obtiene el ID del dispositivo
    snprintf(topic_buf, buf_len, "%s/imu-live", mac_str); // Formatea el tópico como "mac/imu-live"
    ESP_LOGI(TAG, "Tópico MQTT generado: %s", topic_buf); // Log del tópico generado
}

void mqtt_publish_last_sample(esp_mqtt_client_handle_t client, char *topic, volatile data_imu_t *sample)
{
    if (!client || !sample || !topic)
    {
        ESP_LOGE(TAG, "Parámetro inválido para publicación MQTT");
        return;
    }

    if (!mqtt_connected)
    {
        ESP_LOGW(TAG, "MQTT desconectado, no se publica en topic=%s", topic);
        return;
    }

    cJSON *json = cJSON_CreateObject();
    if (!json)
    {
        ESP_LOGE(TAG, "Error al crear objeto JSON");
        return;
    }

    cJSON_AddNumberToObject(json, "accX", sample->accX);
    cJSON_AddNumberToObject(json, "accY", sample->accY);
    cJSON_AddNumberToObject(json, "accZ", sample->accZ);
    cJSON_AddNumberToObject(json, "gyroX", sample->gyroX);
    cJSON_AddNumberToObject(json, "gyroY", sample->gyroY);
    cJSON_AddNumberToObject(json, "gyroZ", sample->gyroZ);
    cJSON_AddNumberToObject(json, "inclX", sample->inclX);
    cJSON_AddNumberToObject(json, "inclY", sample->inclY);

    char *json_str = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    if (!json_str)
    {
        ESP_LOGE(TAG, "Error al generar string JSON");
        return;
    }

    int msg_id = esp_mqtt_client_publish(client, topic, json_str, strlen(json_str), 0, 0);

    /*  if (msg_id >= 0)
     {
         ESP_LOGI(TAG, "Publicación MQTT exitosa: topic=%s, msg_id=%d", topic, msg_id);
     }
     else
     {
         ESP_LOGE(TAG, "Error al publicar en MQTT (msg_id=%d)", msg_id);
     } */

    free(json_str);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    struct device *dev = handler_args;

    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        mqtt_connected = true;
        esp_mqtt_client_publish(event->client, "imu/status", "online", 0, 0, 0);
        ESP_LOGI(TAG, "MQTT conectado");
        break;

    case MQTT_EVENT_DISCONNECTED:
        mqtt_connected = false;
        ESP_LOGW(TAG, "MQTT desconectado");
        break;

    case MQTT_EVENT_ERROR:
        mqtt_connected = false;
        ESP_LOGE(TAG, "MQTT_EVENT_ERROR");
        break;

    default:
        break;
    }
}

void mqtt_app_start(struct device *dev)
{
    get_mqtt_topic(topic, sizeof(topic));

        esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER,
        .broker.address.port = MQTT_PORT,
        .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
        .credentials.username = MQTT_USERNAME,
        .credentials.authentication.password = MQTT_PASSWORD,
        .session.last_will = {
            .topic  = "imu/lastwill",
            .msg    = "offline",
            .qos    = 0,
            .retain = 0,
        },
        .session.keepalive = 60,
        .buffer.out_size = 8192,
    };

    dev->client = esp_mqtt_client_init(&mqtt_cfg);
    assert(dev->client);

    mqtt_client_global = dev->client;

    esp_mqtt_client_register_event(dev->client, ESP_EVENT_ANY_ID, mqtt_event_handler, dev);
    esp_mqtt_client_start(dev->client);

    ESP_LOGI(TAG, "Cliente MQTT iniciado");
}

// Tarea para publicar datos en vivo sin bloquear la recolección
void mqtt_live_task(void *arg)
{
    data_imu_t sample;

    while (1)
    {
        if (xQueueReceive(mqtt_live_queue, &sample, portMAX_DELAY))
        {
            // Publica sin bloquear la recolección
            mqtt_publish_last_sample(mqtt_client_global, topic, &sample);
        }
    }
}