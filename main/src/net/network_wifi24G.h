// connect_wifi.h
#pragma once

#if defined(NETWORK_MODE_WIFI2G)

#include <esp_system.h>
#include <nvs_flash.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "driver/gpio.h"
#include <lwip/sockets.h>
#include <lwip/sys.h>
#include <lwip/api.h>
#include <lwip/netdb.h>
#include "common/utils.h"

extern int wifi_connect_status;

void appWifi2G(void);

int32_t network_wifi2g_get_rssi(void);
bool network_wifi2g_is_connected(void);
const char *network_wifi2g_get_ip(void);
const char *network_wifi2g_get_gateway(void);

#endif // NETWORK_MODE_WIFI2G