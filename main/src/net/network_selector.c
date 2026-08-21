// network_selector.c
#include "net/network_selector.h"
#include "esp_log.h"

static const char *TAG = "NET_SELECTOR";

void appNetworkInit(void)
{
    ESP_LOGI(TAG, "Inicializando red (%s)...", NETWORK_MODE_NAME);

#if defined(NETWORK_MODE_HALOW)

    appHalow(); // función de tu stack HaLow
#elif defined(NETWORK_MODE_WIFI2G)
    appWifi2G(); // función de tu stack WiFi 2.4
#endif
}

void network_get_status(network_status_t *status)
{
    memset(status, 0, sizeof(*status));

#if defined(NETWORK_MODE_HALOW)
    // Usa API del driver HaLow
    status->link_up = network_halow_is_connected();
    strncpy(status->ip, network_halow_get_ip(), sizeof(status->ip));
    strncpy(status->gw, network_halow_get_gateway(), sizeof(status->gw));
    status->rssi = network_halow_get_rssi();
    status->internet_ok = true;
    status->iface = NET_IF_HALOW;

#elif defined(NETWORK_MODE_WIFI2G)
    // Usa API del driver WiFi 2.4G
    status->link_up = network_wifi2g_is_connected();
    strncpy(status->ip, network_wifi2g_get_ip(), sizeof(status->ip));
    strncpy(status->gw, network_wifi2g_get_gateway(), sizeof(status->gw));
    status->rssi = network_wifi2g_get_rssi();
    status->internet_ok = true; // aquí puedes poner el resultado de un ping
    status->iface = NET_IF_WIFI_2G;

#endif
}

const char *network_iface_to_str(network_interface_t iface)
{
    switch (iface)
    {
    case NET_IF_HALOW:
        return "HALOW";
    case NET_IF_WIFI_2G:
        return "WiFi 2G";
    case NET_IF_ETH:
        return "ETH";
    default:
        return "UNKNOWN";
    }
}