// network_selector.h
#pragma once
#include <stdint.h>
#include <stdbool.h>
// -----------------------------------------------------------------------------
// Selección de red por precompilado
// -----------------------------------------------------------------------------
// Define en CMakeLists.txt una de estas opciones:
//
//   add_compile_definitions(NETWORK_MODE_HALOW=1)
//   add_compile_definitions(NETWORK_MODE_WIFI2G=1)
//
// -----------------------------------------------------------------------------
typedef enum
{
    NET_IF_UNKNOWN = 0,
    NET_IF_WIFI_2G,
    NET_IF_HALOW,
    NET_IF_ETH,
    // NET_IF_LTE,
    // NET_IF_WIFI_5G,
} network_interface_t;

typedef struct
{
    bool link_up;              // true = conectado
    char ip[16];               // IPv4 string
    char gw[16];               // Gateway string
    int32_t rssi;              // dBm (INT32_MIN si no disponible)
    bool internet_ok;          // ping o test externo
    network_interface_t iface; // tipo de interfaz
} network_status_t;

#if defined(NETWORK_MODE_HALOW)
#include "network_halow_mm.h"
#define NETWORK_MODE_NAME "HaLow"
#elif defined(NETWORK_MODE_WIFI2G)

#include "network_wifi24G.h"
#define NETWORK_MODE_NAME "WiFi 2.4G"
#else
#error "Debe definir NETWORK_MODE_HALOW o NETWORK_MODE_WIFI2G en CMakeLists.txt"
#endif

#ifdef __cplusplus
extern "C"
{
#endif

    // Inicializa la red según el modo compilado
    void appNetworkInit(void);
    void network_get_status(network_status_t *status);
    const char *network_iface_to_str(network_interface_t iface);
#ifdef __cplusplus
}
#endif
