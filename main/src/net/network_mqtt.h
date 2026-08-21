// network_mqtt.h

#ifndef NETWORK_MQTT_H
#define NETWORK_MQTT_H

#include "mqtt_client.h" // Para que reconozca esp_mqtt_client_handle_t
#include "imu/data_sensor.h" //
#include "imu/imu.h"         // Para que reconozca data_imu_t

#include "net/network_config.h"

// ---------------------------------------------------------
// El resto de la configuración se toma de network_config.h
// ---------------------------------------------------------
// #define MQTT_IMU_TOPIC "imu-live"
extern char topic[32]; // topic mac/imu-live

// Estructura del dispositivo para manejo del cliente MQTT
typedef struct device
{
    esp_mqtt_client_handle_t client;
    // Puedes agregar más campos si necesitas: estado, ID, buffers, etc.
} device_t;

// Cliente MQTT global para facilidad de uso
extern esp_mqtt_client_handle_t mqtt_client_global;

// Inicializa el cliente MQTT
void mqtt_app_start(struct device *dev);



// Tarea para publicar datos en vivo sin bloquear la recolección
void mqtt_live_task(void *arg);
#endif // NETWORK_MQTT_H
