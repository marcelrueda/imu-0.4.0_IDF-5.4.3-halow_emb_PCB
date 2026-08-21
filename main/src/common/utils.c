// utils.c
#include "common/utils.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"

#include "esp_mac.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h" // para esp_timer_get_time()

static const char *TAG = "UTILS";

char device_id[13]; // 12 + null terminator

void get_device_id(char *device_id_buf, size_t buf_len)
{
    assert(buf_len >= 13);

    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_efuse_mac_get_default(mac));
    snprintf(device_id_buf, buf_len, "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "Device: %s", device_id_buf); // Log the device ID
}

// SW1 Button

#define NUM_PAGES 6               // Número de páginas normales
#define DEBOUNCE_MS 200           // tiempo mínimo entre pulsos
volatile uint8_t currentPage = 0; // Página actual
static volatile int64_t last_interrupt_time = 0;
static void IRAM_ATTR sw1_isr_handler(void *arg)
{
    int64_t now = esp_timer_get_time() / 1000; // tiempo en ms

    if ((now - last_interrupt_time) > DEBOUNCE_MS)
    {
        // Avanzar página
        currentPage++;
        if (currentPage >= NUM_PAGES)
        {
            currentPage = 0;
        }

        // Actualizar timestamp del último pulso válido
        last_interrupt_time = now;
    }
}

void init_SW1(void)
{
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_NEGEDGE, // flanco descendente
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << SW1_GPIO),
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE};
    gpio_config(&io_conf);

    // Instalar ISR
    gpio_install_isr_service(0);
    gpio_isr_handler_add(SW1_GPIO, sw1_isr_handler, NULL);
}

void morse_reset_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << MM_RESET_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&io_conf);

    // Mantener el chip liberado normalmente
    gpio_set_level(MM_RESET_GPIO, 1);
}
void morse_reset_pulse(void)
{
    // Forzar reset físico
    gpio_set_level(MM_RESET_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(50)); // 50 ms de pulso bajo
    gpio_set_level(MM_RESET_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(100)); // tiempo de recuperación
}

// LEDs Control
static led_strip_handle_t rgb_strip = NULL;

static void toggle_led_task(void *pvParameter)
{
    bool led_on = false;
    while (1)
    {
        gpio_set_level(LED_GPIO, led_on);
        led_on = !led_on;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void init_status_led(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&io_conf);
}
// Start the LED toggle task
void start_led_toggle_task(void)
{
    xTaskCreate(toggle_led_task, "toggle_led_task", 2048, NULL, 1, NULL);
}

void init_status_rgb(void)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = RGB_GPIO,
        .max_leds = 1,
        .led_model = LED_MODEL_SK6812,
        .flags.invert_out = false,
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000, // 10 MHz
        .flags.with_dma = false,
    };

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &rgb_strip));
    ESP_ERROR_CHECK(led_strip_clear(rgb_strip));
    ESP_LOGI(TAG, "RGB status LED initialized");
}

void set_status_color(uint8_t r, uint8_t g, uint8_t b)
{
    if (rgb_strip)
    {
        led_strip_set_pixel(rgb_strip, 0, r, g, b);
        led_strip_refresh(rgb_strip);
    }
}

void set_status_mode_data_collection(void)
{
    set_status_color(128, 0, 128); // Violeta
}

void set_status_mode_error(void)
{
    set_status_color(255, 0, 0); // Rojo puro
}

void set_status_mode_data_sending(void)
{
    set_status_color(255, 255, 0); // Amarillo
}
void turn_off_status_led(void)
{
    set_status_color(0, 0, 0);
}

// Helper para crear tareas con chequeo y opción de core
BaseType_t create_task(TaskFunction_t task, const char *name, uint32_t stack, void *param,
                       UBaseType_t prio, TaskHandle_t *handle,
                       int core_id) // -1 (tskNO_AFFINITY) para libre, 0 o 1 para fijar core
{
    BaseType_t res;

    if (core_id == tskNO_AFFINITY || core_id < 0)
    {
        res = xTaskCreate(task, name, stack, param, prio, handle);
    }
    else
    {
        res = xTaskCreatePinnedToCore(task, name, stack, param, prio, handle, core_id);
    }

    if (res != pdPASS)
    {
        ESP_LOGE(TAG, "Error creando tarea: %s (stack=%lu, prio=%u, core=%d)",
                 name, (unsigned long)stack, (unsigned)prio, core_id);
    }
    else
    {
        ESP_LOGI(TAG, "Tarea creada: %s (stack=%lu, prio=%u, core=%d)",
                 name, (unsigned long)stack, (unsigned)prio, core_id);
    }

    return res;
}

void fatal_error(const char *msg)
{
    static const char *TAG = "FATAL";
    ESP_LOGE(TAG, "ERROR CRÍTICO: %s", msg);

    // Indicar error con LED RGB
    set_status_mode_error(); // LED rojo

    // Pequeña espera para que se vea el error
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Reset físico del hardware si aplica
    morse_reset_pulse();

    // Reinicio del sistema
    esp_restart();
}

// Function to log memory usage
void log_memory_usage(const char *context)
{
    size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    size_t largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    ESP_LOGI("MEM", "[%s] Heap libre: %u | Bloque más grande: %u | PSRAM: %u",
             context, (unsigned)free_heap, (unsigned)largest_block, (unsigned)free_psram);
}