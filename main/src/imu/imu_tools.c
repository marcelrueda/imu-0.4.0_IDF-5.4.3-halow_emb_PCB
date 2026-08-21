// imu_tools.c
#include "imu_tools.h"
#include "esp_log.h"
#include <string.h>
#include <math.h>
#include "imu.h"
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// STA/LTA (Short-Term Average / Long-Term Average)
#define STA_WINDOW 300             // 10s * 30Hz
#define LTA_WINDOW 1800            // 60s * 30Hz
#define RAPID_THRESHOLD 0.15f      // Umbral más alto para trigger rápido
#define STA_LTA_RATIO 3.0f         // Ratio más conservador
#define REFRACTORY_PERIOD_MS 10000 // Período refractario de 10s

static const char *TAG = "IMU_TOOLS";
static float sta_buffer[STA_WINDOW];
static float lta_buffer[LTA_WINDOW];
static int sta_count = 0, lta_count = 0;
static int sta_index = 0, lta_index = 0;

// Marca de tiempo del último evento STA/LTA
static uint32_t last_event_tick = 0;
static bool buffers_initialized = false;
volatile bool seismicEvent = false; // definición real

// Método simple: RMS de vibraciones o umbral en magnitud de aceleración.
// Se calcula el RMS (Root Mean Square) de cada ventana.
// RMS mide la “energía” promedio de las vibraciones.
float compute_rms(float *buffer, int len)
{
    float sum = 0;
    for (int i = 0; i < len; i++)
        sum += buffer[i] * buffer[i];
    return sqrtf(sum / len);

    // Detección de umbral directo:
    // Si la magnitud de la aceleración total sqrt(accX² + accY² + accZ²) excede cierto umbral (ej. > 0.05 g durante más de 0.5 s),
    // lo consideras evento sísmico.
}

// Calcular la magnitud vectorial sin gravedad (mejor aproximación)
float calculate_magnitude_without_gravity(float accX, float accY, float accZ)
{
    // Calcular la magnitud total
    float total_magnitude = sqrtf(accX * accX + accY * accY + accZ * accZ);

    // Asumir que la gravedad es el componente DC, pero mejor usar filtro paso alto
    static float gravity_filter = 1.0f;
    const float alpha = 0.99f; // Factor de suavizado

    // Filtro para estimar la gravedad
    gravity_filter = alpha * gravity_filter + (1 - alpha) * total_magnitude;

    return total_magnitude - gravity_filter;
}

void initialize_buffers(float initial_value)
{
    for (int i = 0; i < STA_WINDOW; i++)
    {
        sta_buffer[i] = initial_value;
    }
    for (int i = 0; i < LTA_WINDOW; i++)
    {
        lta_buffer[i] = initial_value;
    }
    sta_count = STA_WINDOW;
    lta_count = LTA_WINDOW;
    buffers_initialized = true;
}

void check_for_seismic_event(data_imu_t sample)
{
    // Magnitud vectorial de aceleración
    float mag = calculate_magnitude_without_gravity(sample.accX, sample.accY, sample.accZ);

    // Inicializar buffers si es la primera vez
    if (!buffers_initialized)
    {
        initialize_buffers(fabsf(mag));
        return;
    }

    // Actualizar ventanas circulares
    sta_buffer[sta_index] = fabsf(mag); // Usar valor absoluto
    lta_buffer[lta_index] = fabsf(mag);

    sta_index = (sta_index + 1) % STA_WINDOW;
    lta_index = (lta_index + 1) % LTA_WINDOW;

    if (sta_count < STA_WINDOW)
        sta_count++;
    if (lta_count < LTA_WINDOW)
        lta_count++;

    // Trigger rápido:
    // Si la magnitud absoluta excede cierto umbral (ej. > 0.05 g),
    // se dispara un aviso inmediato sin esperar a STA/LTA.

    if (fabsf(mag) > RAPID_THRESHOLD)

    {
        ESP_LOGW(TAG, "Trigger rápido: vibración fuerte %.3fg", mag);
        seismicEvent = true;
        last_event_tick = xTaskGetTickCount(); // marca de tiempo
                                               // aquí puedes mandar señal a tu máquina de estados o publicar MQTT
        // data_signal_t sig = SIGNAL_SEISMIC_EVENT;
        // xQueueSend(signal_queue_state, &sig, 0);
    }
    else if (xTaskGetTickCount() - last_event_tick > pdMS_TO_TICKS(2000))
    {
        // mantener 2 segundos el evento en pantalla
        seismicEvent = false;
    }

    // Comparación STA/LTA
    // Una vez que ambas ventanas están llenas:
    // Calcula el cociente STA/LTA.
    // Si el STA (vibración reciente) es 3 veces mayor que el LTA (ruido de fondo), se dispara la alarma.
    // Ese umbral 3.0f es el criterio de “evento sísmico”.
    if (sta_count >= STA_WINDOW && lta_count >= LTA_WINDOW)
    {
        float sta = compute_rms(sta_buffer, STA_WINDOW);
        float lta = compute_rms(lta_buffer, LTA_WINDOW);

        uint32_t now = xTaskGetTickCount();

        if (lta > 0.001f && (sta / lta) > STA_LTA_RATIO)
        {
            // Período refractario:
            // Evita que un mismo evento genere múltiples detecciones consecutivas.
            if (now - last_event_tick > pdMS_TO_TICKS(REFRACTORY_PERIOD_MS)) // 10 segundos
            {
                ESP_LOGE(TAG, "POSIBLE SISMO DETECTADO! STA/LTA=%.2f", sta / lta);
                last_event_tick = now;
                seismicEvent = true;
                // aquí puedes mandar señal a tu máquina de estados o publicar MQTT
                // data_signal_t sig = SIGNAL_SEISMIC_EVENT;
                // xQueueSend(signal_queue_state, &sig, 0);
            }
            else
            {
                // Dentro del período refractario, no hacer nada
                seismicEvent = false;
            }
        }

        // Log para debugging
        static uint32_t last_log = 0;
        if (now - last_log > pdMS_TO_TICKS(5000))
        {
            ESP_LOGI(TAG, "STA: %.4f, LTA: %.4f, Ratio: %.2f", sta, lta, sta / lta);
            last_log = now;
        }
    }
}

// Función de calibración opcional (conserva tus comentarios)
/* void calibrate_imu(void)
{
    // Deja que se llenen los buffers con datos normales antes de empezar a detectar
    ESP_LOGI(TAG, "Calibrando IMU, llenando buffers...");
    for (int i = 0; i < LTA_WINDOW; i++) {
        data_imu_t sample = read_imu_data(); // Tu función para leer IMU
        float mag = calculate_magnitude_without_gravity(sample.accX, sample.accY, sample.accZ);

        // Actualizar buffers manualmente para calibración
        if (i < STA_WINDOW) {
            sta_buffer[i] = fabsf(mag);
        }
        lta_buffer[i] = fabsf(mag);

        vTaskDelay(pdMS_TO_TICKS(33)); // ≈30Hz
    }
    sta_count = STA_WINDOW;
    lta_count = LTA_WINDOW;
    buffers_initialized = true;
    ESP_LOGI(TAG, "Calibración completada, buffers llenos");
} */