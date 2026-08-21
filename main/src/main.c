// main.c
#include "bus/bus_spi.h"                // Bus SPI
#include "common/utils.h"               // Utilidades varias
#include "imu/data_sensor.h"            // Datos del sensor
#include "imu/imu.h"                    // IMU
#include "net/network_http_client.h"    // Cliente HTTP
#include "net/network_mqtt.h"           // MQTT
#include "net/network_selector.h"       // Selector de red
#include "peripherals/device_display.h" // Pantalla OLED
#include "peripherals/device_sd.h"      // Tarjeta SD
#include "peripherals/sensor_analog.h"  // ADC
#include "peripherals/max17043.h"       // MAX17043
#include "sys/system_metrics.h"         // Métricas del sistema
#include "sys/time_ntp.h"               // NTP
#include "sys/time_utils.h"             // Utilidades de tiempo

#include "nvs_flash.h"
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "esp_attr.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sleep.h"
#include "esp_system.h"

static const char *TAG = "MAIN";

static device_t dev; // Instancia global del dispositivo

// Estado del sistema
typedef enum
{
  STATE_INIT,
  STATE_SETUP,
  STATE_IDLE,
  STATE_ERROR,
} system_state_t;

static system_state_t current_state = STATE_INIT;

// ---------------- Funciones privadas ----------------

// Inicializa periféricos
static void init_peripherals(void)
{
  sensor_memory_init();    // Inicializa memoria para buffers en PSRAM
  morse_reset_init();      // Inicializa pin de reset físico morse halow
  spi_bus_init(SPI3_HOST); // Inicializa bus SPI

  // Inicialización temprana del display
  appDisplay();
  display_show_loading(5);

  appBattery(); // Inicializa MAX17043

  get_device_id(device_id, sizeof(device_id)); // Obtiene ID del dispositivo
  display_show_loading(15);

  init_status_led(); // Inicializa el LED de estado
  display_show_loading(20);

  start_led_toggle_task(); // Inicia tarea de toggle del LED (VERDE)
  display_show_loading(25);

  // init_status_rgb(); // Inicializa el LED RGB
  display_show_loading(30);

  // analog_input_init(); // Inicializa ADC
  display_show_loading(35);
}

// Inicializa colas
static bool init_queues(void)
{
  signal_queue_task = xQueueCreate(10, sizeof(data_signal_t));
  collection_signal_queue = xQueueCreate(10, sizeof(data_signal_t));
  compression_signal_queue = xQueueCreate(10, sizeof(data_signal_t));
  http_signal_queue = xQueueCreate(10, sizeof(data_signal_t));
  signal_queue_state = xQueueCreate(10, sizeof(data_signal_t));
  mqtt_live_queue = xQueueCreate(10, sizeof(data_imu_t));

  bool ok = signal_queue_task && collection_signal_queue &&
            compression_signal_queue && http_signal_queue &&
            signal_queue_state && mqtt_live_queue;

  if (ok)
    ESP_LOGI(TAG, "Colas de señal creadas correctamente");
  else
    ESP_LOGE(TAG, "Error al crear colas de señal");

  return ok;
}

// Crear tareas de la aplicación
static bool create_app_tasks(void)
{
  BaseType_t ok = pdPASS;
  // ok &= start_system_metrics_task(); // Métricas del sistema

  // Tareas no críticas (Prioridad 2 o 1)
  ok &= create_task(http_send_task, "http_send_task", 4096, NULL, 2, NULL, 0); // BAJA
  ok &= create_task(time_sync_task, "time_sync_task", 4096, NULL, 1, NULL, 0); // BAJA

  // Tareas con prioridad normal (Prioridad 3)
  ok &= create_task(mqtt_live_task, "mqtt_live_task", 4096, NULL, 3, NULL, 0); // NORMAL
  ok &= create_task(display_task, "display_task", 6144, NULL, 3, NULL, 0);     // NORMAL

  // Tareas críticas (Prioridad 5 y 6)
  ok &= create_task(data_compression_task, "data_compression_task", 8192, NULL, 5, &gzip_task_handle, 0); // ALTA
  ok &= create_task(data_collection_task, "data_collection_task", 8192, NULL, 6, NULL, 1);                // CRÍTICA

  if (ok == pdPASS)
    ESP_LOGI(TAG, "Tareas creadas correctamente");
  else
    ESP_LOGE(TAG, "Error al crear tareas");

  return ok == pdPASS;
}

// Log de estados
static void log_state(system_state_t state)
{
  switch (state)
  {
  case STATE_INIT:
    ESP_LOGI(TAG, "IMU 0.1.0 (Built " __DATE__ " " __TIME__ ")");
    ESP_LOGI(TAG, "FSM: INIT → Inicializando sistema...");
    break;
  case STATE_SETUP:
    ESP_LOGI(TAG, "FSM: SETUP → Configurando periféricos...");
    break;
  case STATE_IDLE:
    ESP_LOGI(TAG, "FSM: IDLE → Sistema listo. Esperando eventos...");
    break;
  case STATE_ERROR:
    ESP_LOGE(TAG, "FSM: ERROR → Error crítico detectado.");
    break;
  default:
    ESP_LOGW(TAG, "FSM: Estado desconocido");
    break;
  }
}

// ---------------- FSM ----------------
static void state_machine_task(void *pvParameters)
{
  data_signal_t sig;
  system_state_t last_state = -1; // Para loguear solo cambios

  while (true)
  {
    // Manejo de señales FSM
    if (signal_queue_state != NULL &&
        xQueueReceive(signal_queue_state, &sig, pdMS_TO_TICKS(100)))
    {
      switch (sig)
      {
      case SIGNAL_SEND_OK:
        ESP_LOGI(TAG, "FSM: señal SIGNAL_SEND_OK recibida");
        current_state = STATE_IDLE;
        break;

      default:
        ESP_LOGW(TAG, "FSM: señal desconocida %d", sig);
        break;
      }
    }

    // Log de estado si cambió
    if (last_state != current_state)
    {
      log_state(current_state);
      last_state = current_state;
    }

    // Ejecución de estados
    switch (current_state)
    {
      // Estado de inicialización
    case STATE_INIT:
    {
      display_show_loading(40);
      // Init NVS
      esp_err_t ret = nvs_flash_init();
      if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
          ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
      {
        ESP_LOGW(TAG, "NVS corrupta. Borrando...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
      }
      if (ret != ESP_OK)
      {
        ESP_LOGE(TAG, "Error NVS: %s", esp_err_to_name(ret));
        current_state = STATE_ERROR;
        break;
      }

      display_show_loading(50);

#if defined(NETWORK_MODE_HALOW)
      // Wifi 2.4 tiene su propio event loop, HaLow no
      ESP_ERROR_CHECK(esp_event_loop_create_default());
#endif

      if (!init_queues())
      {
        current_state = STATE_ERROR;
        break;
      }

      display_show_loading(60);
      current_state = STATE_SETUP;
      break;
    }
    // Estado de configuración
    case STATE_SETUP:
      display_show_loading(70);  // 70%
      appNetworkInit();          // Selección de red
      display_show_loading(80);  // 80%
      app_IMU();                 // Inicializa IMU
      display_show_loading(90);  // 90%
      init_SW1();                // Inicializa SW1
      mqtt_app_start(&dev);      // Inicia MQTT
      display_show_loading(100); // 100%

      if (!create_app_tasks())
      {
        current_state = STATE_ERROR;
        break;
      }

      current_state = STATE_IDLE;
      break;
      // Estado de espera
    case STATE_IDLE:
      vTaskDelay(pdMS_TO_TICKS(100)); // cede CPU
      break;
    // Estado de error
    case STATE_ERROR:
      set_status_mode_error();
      vTaskDelay(pdMS_TO_TICKS(2000));
      morse_reset_pulse();
      esp_restart();
      current_state = STATE_INIT;
      break;
    }
  }
}

// ---------------- app_main ----------------
void app_main(void)
{
  init_peripherals();

  BaseType_t res1 = create_task(state_machine_task, "state_machine_task", 8192, NULL, 5, NULL, 1);

  if (res1 != pdPASS)
  {
    fatal_error("No se pudo crear la FSM");
  }
}
