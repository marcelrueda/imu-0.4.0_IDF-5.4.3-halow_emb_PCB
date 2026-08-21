// network_mqtt.c

#include "net/network_mqtt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_crt_bundle.h"
#include "common/utils.h"
#include "esp_timer.h"

#define BATCH_N       10
#define SAMPLE_DT_MS  33
#define G_TO_MS2      9.80665f

static const char *TAG = "MQTT";

static bool mqtt_connected = false;
esp_mqtt_client_handle_t mqtt_client_global = NULL;



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

#define APPEND_ARRAY(NAME, EXPR)                                              \
    do {                                                                      \
        len += snprintf(payload + len, sizeof(payload) - len,                 \
                        ",\"" NAME "\":[");                                   \
        for (int i = 0; i < BATCH_N; i++) {                                   \
            len += snprintf(payload + len, sizeof(payload) - len,             \
                            (i ? ",%.3f" : "%.3f"), (double)(EXPR));          \
        }                                                                     \
        len += snprintf(payload + len, sizeof(payload) - len, "]");           \
    } while (0)

static void mqtt_publish_batch(const data_imu_t *batch, uint32_t t0_ms)
{
    static char payload[2048];
    int len = 0;

    len += snprintf(payload + len, sizeof(payload) - len,
                    "{\"t0\":%lu,\"dt\":%u,\"n\":%u",
                    (unsigned long)t0_ms,
                    (unsigned)SAMPLE_DT_MS,
                    (unsigned)BATCH_N);

    APPEND_ARRAY("ax", batch[i].accX * G_TO_MS2);
    APPEND_ARRAY("ay", batch[i].accY * G_TO_MS2);
    APPEND_ARRAY("az", batch[i].accZ * G_TO_MS2);
    APPEND_ARRAY("gx", batch[i].gyroX);
    APPEND_ARRAY("gy", batch[i].gyroY);
    APPEND_ARRAY("gz", batch[i].gyroZ);
    APPEND_ARRAY("ix", batch[i].inclX);
    APPEND_ARRAY("iy", batch[i].inclY);

    len += snprintf(payload + len, sizeof(payload) - len, "}");

    esp_mqtt_client_publish(mqtt_client_global, MQTT_TELEMETRY_TOPIC,
                            payload, len, 0, 0);
}

// Tarea para publicar datos en vivo sin bloquear la recolección

void mqtt_live_task(void *arg)
{
    static data_imu_t batch[BATCH_N];
    int count = 0;
    uint32_t t0_ms = 0;
    data_imu_t sample;

    while (1)
    {
        if (xQueueReceive(mqtt_live_queue, &sample, portMAX_DELAY))
        {
            if (count == 0) {
                t0_ms = (uint32_t)(esp_timer_get_time() / 1000);
            }
            batch[count++] = sample;

            if (count >= BATCH_N) {
                if (mqtt_connected) {
                    mqtt_publish_batch(batch, t0_ms);
                }
                count = 0;
            }
        }
    }
}


