// network_wifi2.4G.c

#include "net/network_wifi24G.h"
#include "common/utils.h"
#if defined(NETWORK_MODE_WIFI2G)

// #define EXAMPLE_ESP_WIFI_SSID "VARGAS"
// #define EXAMPLE_ESP_WIFI_PASS "Lida2129"
//    #define WIFI_SSID "Prueba"
//    #define WIFI_PASS "apache123"

// WIFI Eleinco
//#define EXAMPLE_ESP_WIFI_SSID "Corporativo"
//#define EXAMPLE_ESP_WIFI_PASS "aabbccddee"

// WIFI Marcel
#define EXAMPLE_ESP_WIFI_SSID "Spitfire4"
#define EXAMPLE_ESP_WIFI_PASS "Sputnik4"
#define EXAMPLE_ESP_MAXIMUM_RETRY 10

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static int s_retry_num = 0;

static bool s_connected = false;
static char s_ip_str[16] = {0};
static char s_gw_str[16] = {0};

static const char *TAG = "WIFI 2G"; // TAG for debug

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY)
        {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retry to connect to the AP");
        }
        else
        {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        s_connected = false;
    }
    else if (event_base == IP_EVENT &&
             event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        esp_ip4addr_ntoa(&event->ip_info.ip, s_ip_str, sizeof(s_ip_str));
        esp_ip4addr_ntoa(&event->ip_info.gw, s_gw_str, sizeof(s_gw_str));

        s_retry_num = 0;
        s_connected = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

        ESP_LOGI(TAG, "Got IP: %s, GW: %s", s_ip_str, s_gw_str);
    }
}

void appWifi2G(void)
{
    // Inicializar Wi-Fi
    ESP_LOGI(TAG, "Iniciando Wi-Fi en modo STA");

    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            /* Setting a password implies station will connect to all security modes including WEP/WPA.
             * However these modes are deprecated and not advisable to be used. Incase your Access point
             * doesn't support WPA2, these mode can be enabled by commenting below line */
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually     * happened. */
    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s", EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    }
    else if (bits & WIFI_FAIL_BIT)
    {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s", EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    }
    else
    {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
    vEventGroupDelete(s_wifi_event_group);
}

const char *network_wifi2g_get_ip(void) { return s_connected ? s_ip_str : "---"; }
const char *network_wifi2g_get_gateway(void) { return s_connected ? s_gw_str : "---"; }

int32_t network_wifi2g_get_rssi(void)
{
    wifi_ap_record_t ap_info;
    if (s_connected && esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK)
    {
        return ap_info.rssi;
    }
    return INT32_MIN; // no disponible
}

bool network_wifi2g_is_connected(void) { return s_connected; }

#endif // NETWORK_MODE_WIFI2G