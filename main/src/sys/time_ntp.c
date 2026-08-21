// time_ntp.c
// Implementación de sincronización NTP manual con soporte de zonas horarias LATAM (incluye DST donde aplica)

#include "time_utils.h"
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"

#define NTP_PORT 123
#define NTP_PACKET_SIZE 48
#define UNIX_OFFSET 2208988800ULL
#define NTP_TIMEOUT_SEC 5
#define UPDATE_INTERVAL_SEC 3600 // Cada hora

static const char *TAG = "NTP";

// Servidores NTP confiables
static const char *ntp_servers[] = {
    "216.239.35.0", // Google
    "129.6.15.28",  // NIST
    "132.163.96.1"  // NTP Pool
};
static const size_t num_servers = sizeof(ntp_servers) / sizeof(ntp_servers[0]);

// Mapa países → TZ (POSIX compatible)
typedef struct
{
    const char *country;
    const char *tz;
} country_tz_t;

static const country_tz_t country_timezones[] = {
    // Región Andina
    {"COLOMBIA", "COT+5"},  // UTC-5, sin DST
    {"PERU", "PET+5"},      // UTC-5, sin DST
    {"ECUADOR", "ECT+5"},   // UTC-5, sin DST
    {"BOLIVIA", "BOT+4"},   // UTC-4, sin DST
    {"VENEZUELA", "VET+4"}, // UTC-4, sin DST

    // Cono Sur
    {"ARGENTINA", "ART+3"},                         // UTC-3, sin DST
    {"CHILE", "CLT+4CLST+3,M9.1.0/0,M4.1.0/0"},     // UTC-4 estándar, DST UTC-3
    {"PARAGUAY", "PYT+4PYST+3,M10.1.0/0,M3.4.0/0"}, // UTC-4 estándar, DST UTC-3
    {"URUGUAY", "UYT+3"},                           // UTC-3, DST abolido 2015
    {"BRASIL", "BRT+3"},                            // UTC-3, DST abolido 2019

    // Centroamérica
    {"MEXICO", "CST+6CDT+5,M4.1.0/2,M10.5.0/2"}, // UTC-6 estándar, DST UTC-5
    {"GUATEMALA", "CST+6"},                      // UTC-6, sin DST
    {"EL_SALVADOR", "CST+6"},                    // UTC-6, sin DST
    {"HONDURAS", "CST+6"},                       // UTC-6, sin DST
    {"NICARAGUA", "CST+6"},                      // UTC-6, sin DST
    {"COSTA_RICA", "CST+6"},                     // UTC-6, sin DST
    {"PANAMA", "EST+5"},                         // UTC-5, sin DST

    // Caribe
    {"CUBA", "CST+5CDT+4,M3.2.0/0,M11.1.0/0"}, // UTC-5 estándar, DST UTC-4
    {"DOMINICANA", "AST+4"},                   // UTC-4, sin DST
    {"PUERTO_RICO", "AST+4"},                  // UTC-4, sin DST
};
static const size_t num_countries = sizeof(country_timezones) / sizeof(country_timezones[0]);

// ==== Implementación de sincronización ====

// Obtener tiempo UTC desde NTP
static int get_utc_time_from_server(const char *server_ip, time_t *out_timestamp)
{
    int sockfd;
    struct sockaddr_in server_addr;
    uint8_t packet[NTP_PACKET_SIZE] = {0};

    packet[0] = 0b00100011; // LI=0, VN=4, Mode=3 (client)

    sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd < 0)
    {
        ESP_LOGE(TAG, "Error creando socket");
        return -1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(NTP_PORT);

    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0)
    {
        ESP_LOGE(TAG, "Error parseando IP del servidor: %s", server_ip);
        close(sockfd);
        return -1;
    }

    struct timeval timeout = {.tv_sec = NTP_TIMEOUT_SEC, .tv_usec = 0};
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    if (sendto(sockfd, packet, NTP_PACKET_SIZE, 0,
               (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        ESP_LOGE(TAG, "Error enviando paquete a %s", server_ip);
        close(sockfd);
        return -1;
    }

    socklen_t addr_len = sizeof(server_addr);
    if (recvfrom(sockfd, packet, NTP_PACKET_SIZE, 0,
                 (struct sockaddr *)&server_addr, &addr_len) < 0)
    {
        ESP_LOGE(TAG, "Timeout o error recibiendo respuesta de %s", server_ip);
        close(sockfd);
        return -1;
    }

    uint32_t seconds = ntohl(*(uint32_t *)(&packet[40]));
    *out_timestamp = seconds - UNIX_OFFSET;

    close(sockfd);
    return 0;
}

// Actualizar hora UTC del sistema
bool update_system_utc_time()
{
    time_t timestamp = 0;
    bool success = false;

    for (size_t i = 0; i < num_servers; i++)
    {
        if (get_utc_time_from_server(ntp_servers[i], &timestamp) == 0)
        {
            success = true;
            break;
        }
    }

    if (!success)
    {
        ESP_LOGW(TAG, "No se pudo sincronizar la hora UTC con los servidores NTP");
        return false;
    }

    struct timeval tv = {.tv_sec = timestamp, .tv_usec = 0};
    if (settimeofday(&tv, NULL) != 0)
    {
        ESP_LOGE(TAG, "Error actualizando hora del sistema");
        return false;
    }

    struct tm tm_utc;
    char buf[64];
    gmtime_r(&timestamp, &tm_utc); // Convierte a UTC
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S UTC", &tm_utc);
    ESP_LOGI(TAG, "Hora UTC sincronizada: %s", buf);
    return true;
}

// Obtener TZ según nombre del país
static const char *get_timezone_for_country(const char *country)
{
    if (!country)
        return "UTC0"; // Default UTC

    for (size_t i = 0; i < num_countries; i++)
    {
        if (strcasecmp(country_timezones[i].country, country) == 0)
        {
            return country_timezones[i].tz;
        }
    }
    return "UTC0"; // Default si no encuentra
}

// Imprimir hora local según país
void print_country_time(const char *country)
{
    const char *tz = get_timezone_for_country(country);
    time_t now;
    struct tm timeinfo;
    char buf[64];

    time(&now);

    setenv("TZ", tz, 1);
    tzset();

    localtime_r(&now, &timeinfo);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %Z", &timeinfo);

    ESP_LOGI(TAG, "Hora local (%s): %s", country ? country : "UTC", buf);
}

// Configura el TZ del sistema según país
void set_system_country_time(const char *country)
{
    const char *tz = get_timezone_for_country(country);
    setenv("TZ", tz, 1);
    tzset(); // Aplica la zona horaria globalmente

    ESP_LOGI(TAG, "Sistema configurado para hora local (%s)", country ? country : "UTC");
}

// Task FreeRTOS para sincronizar hora UTC cada hora
void time_sync_task(void *pvParameters)
{
    while (1)
    {
        ESP_LOGI(TAG, "Actualizando hora UTC desde NTP...");
        update_system_utc_time();            // Sincroniza UTC automáticamente
        set_system_country_time("COLOMBIA"); // Cambia aquí el país deseado
        print_country_time("COLOMBIA");      // Imprime hora local
        vTaskDelay(pdMS_TO_TICKS(UPDATE_INTERVAL_SEC * 1000));
    }
}

// Iniciar task
void start_time_sync_task()
{
    xTaskCreate(time_sync_task, "time_sync_task", 4096, NULL, 5, NULL);
}
