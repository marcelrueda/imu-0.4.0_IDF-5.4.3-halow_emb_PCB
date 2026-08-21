#ifndef NETWORK_CONFIG_H
#define NETWORK_CONFIG_H

/**
 * @file network_config.h
 * @brief Configuración centralizada para los servicios de red (MQTT y HTTP)
 */

// ---------------------------------------------------------
// Configuración del Broker MQTT
// ---------------------------------------------------------
// Configuración del Broker MQTT (EMQX Cloud, TLS)
#define MQTT_BROKER   "mqtts://x1dc1a12.ala.us-east-1.emqxsl.com"
#define MQTT_PORT     8883
#define MQTT_USERNAME "R2MyClient"
#define MQTT_PASSWORD "AmeliaJuan"
#define MQTT_TELEMETRY_TOPIC "imu/esp32/rs20003BELO/telemetry"

// ---------------------------------------------------------
// Configuración del Cliente HTTP
// ---------------------------------------------------------
 #define HTTP_SERVER_URL "http://190.144.89.234:3001"   // Remoto
//#define HTTP_SERVER_URL "http://192.168.1.149:3000" // Local
#define HTTP_UPLOAD_PATH "/api/upload/uploadGzip"

// ---------------------------------------------------------
// Otros ajustes de red
// ---------------------------------------------------------
#define MAX_URL_LENGTH 256
#define HTTP_TIMEOUT_MS 10000
#define MAX_RETRIES 3

#endif // NETWORK_CONFIG_H
