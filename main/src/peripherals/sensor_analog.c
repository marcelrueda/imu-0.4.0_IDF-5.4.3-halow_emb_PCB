/**
 * @file sensor_analog.c
 * @version 1.04
 * @brief Lectura de voltaje de batería con ADC1_CH4 (IO5)
 *
 * Correcciones:
 * - Manejo de saturación: se limita a 4.20 V máximo
 * - Factor de corrección basado en divisor resistivo 27k/33k
 * - Porcentaje de carga estimado
 *
 * @author Carlos Andrés
 * @date   2025-09-27
 */

#include <math.h>
#include "sensor_analog.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"

static const char *TAG = "ADC";

// Configuración ADC
#define ADC_CHANNEL ADC_CHANNEL_4 // IO5 = ADC1_CH4
#define ADC_ATTEN ADC_ATTEN_DB_12 // Rango ≈ 0–2.45 V seguro
#define ADC_WIDTH ADC_BITWIDTH_12 // 12 bits (0–4095)
#define NUM_SAMPLES 32            // Promedio de muestras

// Factor divisor resistivo: 27k / 33k
#define VOLTAGE_DIVIDER_FACTOR ((27.0 + 33.0) / 33.0) // ≈ 1.818

// Voltaje de referencia real del ADC
#define ADC_REF_VOLTAGE 2.45f // seguro según datasheet ESP32-S3

// Máximo voltaje esperado en batería (Li-Ion)
#define BATTERY_MAX_VOLTAGE 4.20f
#define BATTERY_MIN_VOLTAGE 3.00f

// Handles del ADC
static adc_oneshot_unit_handle_t adc_handle = NULL;
static adc_cali_handle_t adc_cali_handle = NULL;

#define EMA_ALPHA 0.1f // entre 0.05 y 0.2 suele ir bien

 float vbatt_filtered = 0.0f;

// Porcentaje de carga basado en curva Li-Ion típica
static const float battery_curve[][2] = {
    {4.20f, 100.0f},
    {4.15f, 95.0f},
    {4.05f, 85.0f},
    {3.95f, 75.0f},
    {3.85f, 65.0f},
    {3.75f, 55.0f},
    {3.70f, 45.0f},
    {3.65f, 35.0f},
    {3.55f, 25.0f},
    {3.45f, 15.0f},
    {3.30f, 5.0f},
    {3.00f, 0.0f}};

#define BATTERY_POINTS (sizeof(battery_curve) / sizeof(battery_curve[0]))

// Inicialización del ADC
void analog_input_init(void)
{
    ESP_LOGI(TAG, "Inicializando ADC en IO5 (ADC1_CH4)");

    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc_handle));

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_WIDTH,
        .atten = ADC_ATTEN,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_CHANNEL, &config));

    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN,
        .bitwidth = ADC_WIDTH,
    };
    if (adc_cali_create_scheme_curve_fitting(&cali_config, &adc_cali_handle) != ESP_OK)
    {
        ESP_LOGE(TAG, "Falló la calibración, usando método manual");
        adc_cali_handle = NULL;
    }
    analog_input_diagnostic();                    // Diagnóstico inicial
    vbatt_filtered = analog_input_read_battery(); // Primera lectura
}

// Leer voltaje de batería con calibración
float analog_input_read_battery(void)
{
    uint32_t sum = 0;
    int raw;

    for (int i = 0; i < NUM_SAMPLES; i++)
    {
        ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, ADC_CHANNEL, &raw));
        sum += raw;
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    int raw_avg = sum / NUM_SAMPLES;
    float adc_voltage;

    if (adc_cali_handle != NULL)
    {
        int voltage_mv;
        if (adc_cali_raw_to_voltage(adc_cali_handle, raw_avg, &voltage_mv) == ESP_OK)
        {
            adc_voltage = (float)voltage_mv / 1000.0f;
        }
        else
        {
            adc_voltage = ((float)raw_avg / 4095.0f) * ADC_REF_VOLTAGE;
        }
    }
    else
    {
        adc_voltage = ((float)raw_avg / 4095.0f) * ADC_REF_VOLTAGE;
    }

    // Escalar al voltaje real de batería
    float bat_voltage = adc_voltage * VOLTAGE_DIVIDER_FACTOR;

    if (bat_voltage > BATTERY_MAX_VOLTAGE)
        bat_voltage = BATTERY_MAX_VOLTAGE;
    else if (bat_voltage < BATTERY_MIN_VOLTAGE)
        bat_voltage = BATTERY_MIN_VOLTAGE;
    vbatt_filtered = (EMA_ALPHA * bat_voltage) + ((1.0f - EMA_ALPHA) * vbatt_filtered); // Filtro EMA
    ESP_LOGI(TAG, "RAW=%d, ADC=%.3f V, BAT=%.3f V", raw_avg, adc_voltage, bat_voltage);
    ESP_LOGI(TAG, "RAW=%d, ADC=%.3f V, BAT(raw)=%.3f V, BAT(filtrado)=%.3f V", raw_avg, adc_voltage, bat_voltage, vbatt_filtered);
    return vbatt_filtered;
}

// Porcentaje de carga
int battery_get_percentage(float v)
{
    // Fuera de rango
    if (v >= battery_curve[0][0])
        return 100;
    if (v <= battery_curve[BATTERY_POINTS - 1][0])
        return 0;

    // Buscar segmento adecuado
    for (int i = 0; i < BATTERY_POINTS - 1; i++)
    {
        float v_high = battery_curve[i][0];
        float p_high = battery_curve[i][1];
        float v_low = battery_curve[i + 1][0];
        float p_low = battery_curve[i + 1][1];

        if (v <= v_high && v >= v_low)
        {
            // Interpolación lineal entre dos puntos
            float pct = p_low + (p_high - p_low) * ((v - v_low) / (v_high - v_low));
            return (int)roundf(pct);
        }
    }

    return 0; // fallback (no debería llegar acá)
}

// Diagnóstico del ADC
void analog_input_diagnostic(void)
{
    ESP_LOGI(TAG, "=== DIAGNÓSTICO ADC ===");

    for (int i = 0; i < 5; i++)
    {
        int raw;
        ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, ADC_CHANNEL, &raw));
        ESP_LOGI(TAG, "Lectura %d: RAW=%d", i + 1, raw);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// Tarea periódica de lectura
void analog_input_task(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(1000));
    analog_input_diagnostic();

    while (1)
    {
        float vbatt = analog_input_read_battery();
        int pct = (int)roundf(battery_get_percentage(vbatt_filtered)); // Calcular porcentaje
        ESP_LOGI(TAG, "Batería: %.2f V (%d%%)", vbatt, pct);           // Mostrar porcentaje

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void appADC(void)
{
    analog_input_init();
    xTaskCreate(analog_input_task, "ADC_Task", 4096, NULL, tskIDLE_PRIORITY, NULL);
}
