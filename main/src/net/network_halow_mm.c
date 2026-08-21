
// network_halow_mm.c
// Morse Micro
// Main Chip MM6108IQ
// Morse Micro SDK para esp32 IDF 5.1.1

#include "network_halow_mm.h"
static const char *TAG = "Halow_MM";
extern struct mmipal_link_status g_link_status;
extern bool g_has_link_status;

static char s_ip_str[16] = {0};
static char s_gw_str[16] = {0};
static bool s_internet_ok = false;

// Función para verificar el estado de la conexión
bool wait_for_connection(uint32_t timeout_ms)
{
    uint32_t start = mmosal_get_time_ms();

    while (1)
    {
        enum mmwlan_sta_state state = mmwlan_get_sta_state();

        switch (state)
        {
        case MMWLAN_STA_CONNECTED:
            printf("Estoy conectado a una red.\n");
            return true;

        case MMWLAN_STA_CONNECTING:
            printf("Intentando conectar...\n");
            break;

        case MMWLAN_STA_DISABLED:
            printf("No estoy conectado.\n");
            break;

        default:
            printf("Estado de conexión desconocido.\n");
            break;
        }

        // ¿timeout?
        if (timeout_ms != UINT32_MAX && (mmosal_get_time_ms() - start) > timeout_ms)
        {
            printf("Timeout esperando conexión\n");
            return false;
        }

        // Dormir un poco para no quemar CPU
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

bool network_halow_is_connected(void)
{
    if (g_has_link_status && g_link_status.link_state == MMIPAL_LINK_UP)
    {
        return true;
    }
    return false;
}

int32_t network_halow_get_rssi(void)
{
    return network_halow_is_connected() ? mmwlan_get_rssi() : INT32_MIN;
}

const char *network_halow_get_gateway(void)
{
    if (!network_halow_is_connected())
    {
        return "---";
    }
    strncpy(s_gw_str, g_link_status.gateway, sizeof(s_gw_str));
    s_gw_str[sizeof(s_gw_str) - 1] = '\0';
    return s_gw_str;
}

const char *network_halow_get_ip(void)
{
    if (!network_halow_is_connected())
    {
        return "---";
    }
    strncpy(s_ip_str, g_link_status.ip_addr, sizeof(s_ip_str));
    s_ip_str[sizeof(s_ip_str) - 1] = '\0';
    return s_ip_str;
}

// Función principal
void appHalow(void)
{
    // Inicializa y conecta Wi-Fi (bloquea hasta conexión exitosa)
    app_wlan_init();
    app_wlan_start();

    // Bloquear hasta que realmente haya red (ej: 30 segundos de timeout)
    if (wait_for_connection(30000))
    {
        ESP_LOGI(TAG, "Ya tengo red");
        int rssi = mmwlan_get_rssi(); // Supongamos que existe esta función en la API
        printf("Nivel de señal RSSI: %d dBm\n", rssi);
    }
    else
    {
        ESP_LOGE(TAG, "No logré conexión, abortando");
    }
}
