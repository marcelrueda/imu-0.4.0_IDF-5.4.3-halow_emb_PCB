#ifndef HALLOW_MM_H
#define HALLOW_MM_H

#include <endian.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h> // para sleep

#include "mmhal.h"
#include "mmosal.h"
#include "mmwlan.h"
#include "mmipal.h"
// #include "mmiperf.h"
#include "mm_app_common.h"

#include "esp_mac.h"
#include "esp_log.h"
#include <lwip/sockets.h>

void appHalow(void);
// bool check_connection_status(void)
bool wait_for_connection(uint32_t timeout_ms);
int32_t network_halow_get_rssi(void);
bool network_halow_is_connected(void);
const char *network_halow_get_gateway(void);
const char *network_halow_get_ip(void);

#endif
