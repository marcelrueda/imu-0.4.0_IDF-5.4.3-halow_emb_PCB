// max17043.c - Driver para el sensor de batería MAX17043

#include "peripherals/max17043.h"
#include "bus/bus_i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include <stdbool.h>
#include <string.h>
#include <math.h>

static const char *TAG = "MAX17043";

// Variable global para el sensor MAX17043
max17043_t battery;

// Variables estáticas para la tarea
static uint8_t s_alert_count = 0;
static bool s_battery_initialized = false;

// ============================================================================
// FUNCIONES AUXILIARES I2C
// ============================================================================

/**
 * @brief Lee un registro de 16 bits del MAX17043
 */
static esp_err_t read_register(max17043_t *sensor, uint8_t reg, uint16_t *value)
{
    if (sensor == NULL || value == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t data[2] = {0};
    esp_err_t ret;

    ret = i2c_master_transmit_receive(
        sensor->dev_handle,
        &reg, 1,
        data, 2,
        pdMS_TO_TICKS(100));

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Error leyendo registro 0x%02X: %s", reg, esp_err_to_name(ret));
        return ret;
    }

    // El MAX17043 usa big-endian (MSB primero)
    *value = (data[0] << 8) | data[1];
    return ESP_OK;
}

/**
 * @brief Escribe un registro de 16 bits del MAX17043
 */
static esp_err_t write_register(max17043_t *sensor, uint8_t reg, uint16_t value)
{
    if (sensor == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t data[3] = {
        reg,
        (value >> 8) & 0xFF,
        value & 0xFF};

    esp_err_t ret = i2c_master_transmit(
        sensor->dev_handle,
        data, 3,
        pdMS_TO_TICKS(100));

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Error escribiendo registro 0x%02X: %s", reg, esp_err_to_name(ret));
    }
    return ret;
}

// ============================================================================
// FUNCIONES DE VALIDACIÓN
// ============================================================================

/**
 * @brief Verifica si el voltaje está en rango válido
 */
static bool is_voltage_valid(float voltage)
{
    return (voltage >= MAX17043_VOLTAGE_MIN && voltage <= MAX17043_VOLTAGE_MAX);
}

/**
 * @brief Verifica si el SOC está en rango válido
 */
static bool is_soc_valid(float soc)
{
    return (soc >= MAX17043_SOC_MIN && soc <= MAX17043_SOC_MAX);
}

/**
 * @brief Convierte el valor crudo del registro VCELL a voltios.
 *
 * El registro VCELL tiene el ADC de 12 bits justificado a la izquierda:
 * bits [15:4] = dato, bits [3:0] = siempre 0 (relleno). El peso de 1.25mV
 * por LSB corresponde al valor de 12 bits, así que primero hay que
 * desplazar 4 bits a la derecha (raw >> 4) y luego multiplicar.
 * NO multiplicar el registro de 16 bits completo directamente, o el
 * voltaje sale 16 veces más grande de lo real (p. ej. 66V en vez de ~4.1V).
 */
static inline float vcell_raw_to_voltage(uint16_t raw)
{
    return (raw >> 4) * 0.00125f;
}

/**
 * @brief Lee el flag ALRT (bit 5) directamente del registro CONFIG.
 *        Centraliza la lectura correcta de la alerta para evitar que se
 *        vuelva a confundir con el registro SOC en el futuro.
 */
static esp_err_t read_alert_flag(max17043_t *sensor, uint8_t *alert_out)
{
    uint16_t config;
    esp_err_t ret = read_register(sensor, MAX17043_REG_CONFIG, &config);
    if (ret != ESP_OK)
    {
        return ret;
    }
    *alert_out = (config & MAX17043_ALERT_BIT) ? 1 : 0;
    return ESP_OK;
}

/**
 * @brief Verifica si hay batería conectada
 */
bool max17043_is_battery_present(max17043_t *sensor)
{
    if (sensor == NULL)
    {
        return false;
    }

    uint16_t raw_vcell;
    esp_err_t ret = read_register(sensor, MAX17043_REG_VCELL, &raw_vcell);
    if (ret != ESP_OK)
    {
        return false;
    }

    float voltage = vcell_raw_to_voltage(raw_vcell);
    bool present = is_voltage_valid(voltage);

    sensor->battery_present = present;
    return present;
}

// ============================================================================
// FUNCIONES DE INICIALIZACIÓN
// ============================================================================

/**
 * @brief Inicialización completa del sensor
 */
static esp_err_t max17043_full_init(max17043_t *sensor)
{
    esp_err_t ret;

    // 1. Verificar que el sensor responde leyendo la versión
    uint16_t version;
    ret = read_register(sensor, MAX17043_REG_VERSION, &version);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Sensor no responde en I2C");
        return ret;
    }

    ESP_LOGI(TAG, "Versión del sensor: 0x%04X", version);

    // La versión esperada para MAX17043 es 0x0010
    if (version != 0x0010)
    {
        ESP_LOGW(TAG, "Versión inesperada: 0x%04X (esperada 0x0010)", version);
    }

    // 2. Verificar si hay batería conectada
    if (!max17043_is_battery_present(sensor))
    {
        ESP_LOGW(TAG, "Batería no detectada o voltaje fuera de rango");
        // No fallamos, solo advertimos
    }

    // 3. Configurar el sensor (registro CONFIG, Figure 5 del datasheet)
    uint16_t config;
    ret = read_register(sensor, MAX17043_REG_CONFIG, &config);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Error leyendo registro CONFIG");
        return ret;
    }

    // - Bit 7 (SLEEP)     = 0 -> modo activo
    // - Bits 4-0 (ATHD)   -> umbral de alerta. El chip lo codifica como
    //   "% real = 32 - ATHD", así que para un umbral de 10% hay que
    //   escribir ATHD = 32 - 10 = 22 (0x16), NO el valor 10 directo.
    const uint8_t threshold_percent = 10;
    const uint8_t athd = (uint8_t)(32 - threshold_percent);

    config &= ~MAX17043_CONFIG_SLEEP_BIT; // Quitar sleep
    config = (config & 0xFFE0) | (athd & MAX17043_ATHD_MASK);

    ret = write_register(sensor, MAX17043_REG_CONFIG, config);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Error escribiendo registro CONFIG");
        return ret;
    }

    // 4. Ejecutar Quick Start por software.
    //    IMPORTANTE: Quick Start se escribe en el registro MODE (0x06),
    //    no en el registro COMMAND (0xFE, reservado para reset/POR).
    ESP_LOGI(TAG, "Ejecutando Quick Start por software...");
    ret = write_register(sensor, MAX17043_REG_MODE, MAX17043_CMD_QUICK_START);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Error ejecutando Quick Start");
        return ret;
    }

    // Esperar a que el sensor complete la medición (mínimo 500ms según datasheet)
    vTaskDelay(pdMS_TO_TICKS(500));

    // 5. Leer valores iniciales para verificar
    uint16_t raw_vcell, raw_soc;
    ret = read_register(sensor, MAX17043_REG_VCELL, &raw_vcell);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Error leyendo VCELL inicial");
        return ret;
    }

    ret = read_register(sensor, MAX17043_REG_SOC, &raw_soc);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Error leyendo SOC inicial");
        return ret;
    }

    sensor->voltage = vcell_raw_to_voltage(raw_vcell);
    sensor->soc = raw_soc * 0.00390625f;

    if (!is_soc_valid(sensor->soc))
    {
        ESP_LOGW(TAG, "SOC inicial fuera de rango: %.2f%%", sensor->soc);
    }

    uint8_t alert = 0;
    if (read_alert_flag(sensor, &alert) == ESP_OK)
    {
        sensor->alert = alert;
    }

    ESP_LOGI(TAG, "Inicialización completada - Batería: %.3fV, SOC: %.1f%%",
             sensor->voltage, sensor->soc);

    return ESP_OK;
}

/**
 * @brief Inicializa el sensor MAX17043
 */
esp_err_t max17043_init(HelperI2C *bus, max17043_t *sensor)
{
    if (sensor == NULL || bus == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    // Limpiar estructura
    memset(sensor, 0, sizeof(max17043_t));

    // Agregar dispositivo al bus I2C
    esp_err_t ret = helper_i2c_add_device(
        bus,
        &sensor->i2c_device,
        MAX17043_I2C_ADDR,
        I2C0_FREQ_HZ);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Error añadiendo dispositivo I2C al bus (%s)",
                 esp_err_to_name(ret));
        return ret;
    }

    sensor->dev_handle = sensor->i2c_device.slave_handle;

    // Intentar inicializar con reintentos
    int max_retries = 3;
    for (int i = 0; i < max_retries; i++)
    {
        ret = max17043_full_init(sensor);
        if (ret == ESP_OK)
        {
            ESP_LOGI(TAG, "MAX17043 inicializado correctamente");
            return ESP_OK;
        }

        ESP_LOGW(TAG, "Intento %d/%d falló, reintentando en 500ms...",
                 i + 1, max_retries);
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    // Si todos los intentos fallaron, eliminar el dispositivo del bus
    ESP_LOGE(TAG, "No se pudo inicializar el MAX17043 después de %d intentos",
             max_retries);
    i2c_master_bus_rm_device(sensor->dev_handle);
    return ESP_ERR_TIMEOUT;
}

// ============================================================================
// FUNCIONES DE LECTURA
// ============================================================================

esp_err_t max17043_read_voltage(max17043_t *sensor, float *voltage)
{
    if (sensor == NULL || voltage == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t raw;
    esp_err_t ret = read_register(sensor, MAX17043_REG_VCELL, &raw);
    if (ret != ESP_OK)
    {
        return ret;
    }

    // Conversión: ADC de 12 bits justificado a la izquierda -> raw >> 4,
    // luego 1 LSB = 1.25mV
    *voltage = vcell_raw_to_voltage(raw);
    sensor->voltage = *voltage;

    return ESP_OK;
}

esp_err_t max17043_read_soc(max17043_t *sensor, float *soc)
{
    if (sensor == NULL || soc == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t raw;
    esp_err_t ret = read_register(sensor, MAX17043_REG_SOC, &raw);
    if (ret != ESP_OK)
    {
        return ret;
    }

    // Conversión: 1 LSB = 0.00390625% (1/256)
    *soc = raw * 0.00390625f;

    if (!is_soc_valid(*soc))
    {
        ESP_LOGW(TAG, "SOC fuera de rango: %.2f%%", *soc);
    }

    sensor->soc = *soc;
    // Nota: el flag de alerta NO vive en el registro SOC. Si necesitas el
    // estado de alerta actualizado, usa max17043_get_alert() o max17043_read_all().

    return ESP_OK;
}

esp_err_t max17043_read_all(max17043_t *sensor)
{
    if (sensor == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t raw_vcell, raw_soc;
    esp_err_t ret;

    // Leer voltaje
    ret = read_register(sensor, MAX17043_REG_VCELL, &raw_vcell);
    if (ret != ESP_OK)
    {
        return ret;
    }

    // Leer SOC
    ret = read_register(sensor, MAX17043_REG_SOC, &raw_soc);
    if (ret != ESP_OK)
    {
        return ret;
    }

    // Verificar que los datos no son inválidos
    if (raw_vcell == 0x0000 || raw_vcell == 0xFFFF)
    {
        ESP_LOGW(TAG, "VCELL inválido: 0x%04X", raw_vcell);
        return ESP_ERR_INVALID_RESPONSE;
    }

    // Convertir valores
    sensor->voltage = vcell_raw_to_voltage(raw_vcell);
    sensor->soc = raw_soc * 0.00390625f;

    if (!is_soc_valid(sensor->soc))
    {
        ESP_LOGW(TAG, "SOC fuera de rango: %.2f%%, se satura al límite válido", sensor->soc);
        sensor->soc = (sensor->soc < MAX17043_SOC_MIN) ? MAX17043_SOC_MIN : MAX17043_SOC_MAX;
    }

    // Refrescar el flag de alerta leyendo el registro CONFIG (bit ALRT, bit 5).
    // Esto añade una tercera transacción I2C, pero es necesario porque el
    // flag NO está en el registro SOC.
    uint8_t alert = sensor->alert;
    esp_err_t alert_ret = read_alert_flag(sensor, &alert);
    if (alert_ret == ESP_OK)
    {
        sensor->alert = alert;
    }
    else
    {
        ESP_LOGW(TAG, "No se pudo refrescar el flag de alerta: %s", esp_err_to_name(alert_ret));
    }

    // Verificar si hay batería conectada
    sensor->battery_present = is_voltage_valid(sensor->voltage);

    // Si no hay batería, forzar SOC a 0
    if (!sensor->battery_present)
    {
        if (sensor->voltage < MAX17043_VOLTAGE_MIN)
        {
            ESP_LOGW(TAG, "Voltaje muy bajo: %.3fV (posible batería desconectada)",
                     sensor->voltage);
            sensor->soc = 0.0f;
        }
        else if (sensor->voltage > MAX17043_VOLTAGE_MAX)
        {
            ESP_LOGE(TAG, "Voltaje fuera de rango: %.3fV", sensor->voltage);
            return ESP_ERR_INVALID_RESPONSE;
        }
    }

    return ESP_OK;
}

// ============================================================================
// FUNCIONES DE ALERTA
// ============================================================================

esp_err_t max17043_set_alert_threshold(max17043_t *sensor, uint8_t threshold_percent)
{
    if (sensor == NULL || threshold_percent < 1 || threshold_percent > 32)
    {
        return ESP_ERR_INVALID_ARG;
    }

    // Leer configuración actual
    uint16_t config;
    esp_err_t ret = read_register(sensor, MAX17043_REG_CONFIG, &config);
    if (ret != ESP_OK)
    {
        return ret;
    }

    // El registro ATHD codifica el umbral como: % real = 32 - ATHD
    // (ATHD = 00000b -> 32%, ATHD = 11111b -> 1%). Por eso NO se puede
    // escribir el porcentaje deseado directamente en esos 5 bits.
    uint8_t athd = (uint8_t)(32 - threshold_percent);

    // Limpiar bits de threshold (bits 0-4) y establecer nuevo valor
    config = (config & 0xFFE0) | (athd & MAX17043_ATHD_MASK);

    ret = write_register(sensor, MAX17043_REG_CONFIG, config);
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "Umbral de alerta configurado al %d%% (ATHD=0x%02X)",
                 threshold_percent, athd);
    }

    return ret;
}

esp_err_t max17043_get_alert(max17043_t *sensor, uint8_t *alert)
{
    if (sensor == NULL || alert == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    // El flag ALRT vive en el bit 5 del registro CONFIG, no en el SOC.
    esp_err_t ret = read_alert_flag(sensor, alert);
    if (ret != ESP_OK)
    {
        return ret;
    }

    sensor->alert = *alert;
    return ESP_OK;
}

esp_err_t max17043_clear_alert(max17043_t *sensor)
{
    if (sensor == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    // Para limpiar la alerta hay que hacer read-modify-write del registro
    // CONFIG, apagando el bit ALRT (bit 5). Escribir en el registro COMMAND
    // no tiene ningún efecto sobre esta alerta.
    uint16_t config;
    esp_err_t ret = read_register(sensor, MAX17043_REG_CONFIG, &config);
    if (ret != ESP_OK)
    {
        return ret;
    }

    config &= ~MAX17043_ALERT_BIT;

    ret = write_register(sensor, MAX17043_REG_CONFIG, config);
    if (ret == ESP_OK)
    {
        sensor->alert = 0;
    }

    return ret;
}

// ============================================================================
// FUNCIONES DE CONTROL
// ============================================================================

esp_err_t max17043_quick_start(max17043_t *sensor)
{
    if (sensor == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Ejecutando Quick Start");
    // Quick Start se escribe en el registro MODE (0x06), según el datasheet.
    esp_err_t ret = write_register(sensor, MAX17043_REG_MODE, MAX17043_CMD_QUICK_START);
    if (ret == ESP_OK)
    {
        // Esperar a que complete la medición
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    return ret;
}

esp_err_t max17043_sleep(max17043_t *sensor)
{
    if (sensor == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t config;
    esp_err_t ret = read_register(sensor, MAX17043_REG_CONFIG, &config);
    if (ret != ESP_OK)
    {
        return ret;
    }

    config |= MAX17043_CONFIG_SLEEP_BIT; // Activar sleep
    ret = write_register(sensor, MAX17043_REG_CONFIG, config);

    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "Sensor en modo sleep");
    }

    return ret;
}

esp_err_t max17043_wakeup(max17043_t *sensor)
{
    if (sensor == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t config;
    esp_err_t ret = read_register(sensor, MAX17043_REG_CONFIG, &config);
    if (ret != ESP_OK)
    {
        return ret;
    }

    config &= ~MAX17043_CONFIG_SLEEP_BIT; // Desactivar sleep
    ret = write_register(sensor, MAX17043_REG_CONFIG, config);

    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "Sensor despertado del modo sleep");
        // Después de despertar, ejecutar Quick Start
        vTaskDelay(pdMS_TO_TICKS(10));
        max17043_quick_start(sensor);
    }

    return ret;
}

esp_err_t max17043_reset(max17043_t *sensor)
{
    if (sensor == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    // El registro COMMAND (0xFE) se usa para el Power-On-Reset, escribiendo
    // el valor 0x5400. Antes esta dirección se usaba (incorrectamente) para
    // el Quick Start; ahora queda libre para su función real según datasheet.
    esp_err_t ret = write_register(sensor, MAX17043_REG_CMD, MAX17043_CMD_RESET);
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "Reset (POR) enviado al MAX17043");
        vTaskDelay(pdMS_TO_TICKS(15));
    }
    return ret;
}

// ============================================================================
// FUNCIONES DE DIAGNÓSTICO
// ============================================================================

void max17043_diagnostic(max17043_t *sensor)
{
    if (sensor == NULL)
    {
        ESP_LOGE(TAG, "Error: sensor NULL");
        return;
    }

    uint16_t value;
    ESP_LOGI(TAG, "╔═══════════════════════════════════════╗");
    ESP_LOGI(TAG, "║     DIAGNÓSTICO MAX17043             ║");
    ESP_LOGI(TAG, "╚═══════════════════════════════════════╝");

    // Leer versión
    if (read_register(sensor, MAX17043_REG_VERSION, &value) == ESP_OK)
    {
        ESP_LOGI(TAG, "VERSION   : 0x%04X %s", value,
                 (value == 0x0010) ? "✅ OK" : "⚠️ Inesperada");
    }
    else
    {
        ESP_LOGE(TAG, "VERSION   : ERROR DE LECTURA");
    }

    // Leer configuración (aquí vive también el flag ALRT y el umbral ATHD)
    if (read_register(sensor, MAX17043_REG_CONFIG, &value) == ESP_OK)
    {
        uint8_t athd = value & MAX17043_ATHD_MASK;
        ESP_LOGI(TAG, "CONFIG    : 0x%04X", value);
        ESP_LOGI(TAG, "  Sleep   : %s", (value & MAX17043_CONFIG_SLEEP_BIT) ? "ACTIVO" : "INACTIVO");
        ESP_LOGI(TAG, "  Alerta  : %s", (value & MAX17043_ALERT_BIT) ? "⚠️ ACTIVA" : "✅ INACTIVA");
        ESP_LOGI(TAG, "  Umbral  : %d%% (ATHD=0x%02X)", 32 - athd, athd);
    }
    else
    {
        ESP_LOGE(TAG, "CONFIG    : ERROR DE LECTURA");
    }

    // Leer voltaje
    if (read_register(sensor, MAX17043_REG_VCELL, &value) == ESP_OK)
    {
        float v = vcell_raw_to_voltage(value);
        bool valid = is_voltage_valid(v);
        ESP_LOGI(TAG, "VCELL     : 0x%04X (%.3fV) %s", value, v,
                 valid ? "✅" : "⚠️");
    }
    else
    {
        ESP_LOGE(TAG, "VCELL     : ERROR DE LECTURA");
    }

    // Leer SOC
    if (read_register(sensor, MAX17043_REG_SOC, &value) == ESP_OK)
    {
        float soc = value * 0.00390625f;
        bool valid = is_soc_valid(soc);
        ESP_LOGI(TAG, "SOC       : 0x%04X (%.1f%%) %s", value, soc,
                 valid ? "✅" : "⚠️");
    }
    else
    {
        ESP_LOGE(TAG, "SOC       : ERROR DE LECTURA");
    }

    // Leer modo
    if (read_register(sensor, MAX17043_REG_MODE, &value) == ESP_OK)
    {
        ESP_LOGI(TAG, "MODE      : 0x%04X", value);
    }
    else
    {
        ESP_LOGE(TAG, "MODE      : ERROR DE LECTURA");
    }

    // Estado de la batería
    ESP_LOGI(TAG, "─────────────────────────────────────");
    ESP_LOGI(TAG, "Batería   : %s",
             sensor->battery_present ? "CONECTADA" : "DESCONECTADA");
    ESP_LOGI(TAG, "Voltaje   : %.3fV", sensor->voltage);
    ESP_LOGI(TAG, "SOC       : %.1f%%", sensor->soc);
    ESP_LOGI(TAG, "╚═══════════════════════════════════════╝");
}

// ============================================================================
// FUNCIONES AUXILIARES PARA EL DISPLAY
// ============================================================================

/**
 * @brief Obtiene el estado actual de la batería (lectura más reciente)
 * @param voltage Puntero donde almacenar el voltaje (opcional)
 * @param soc Puntero donde almacenar el SOC (opcional)
 * @param alert Puntero donde almacenar el estado de alerta (opcional)
 * @return true si hay batería conectada, false en caso contrario
 */
bool battery_get_status(float *voltage, float *soc, uint8_t *alert)
{
    if (voltage)
        *voltage = battery.voltage;
    if (soc)
        *soc = battery.soc;
    if (alert)
        *alert = battery.alert;
    return battery.battery_present;
}

/**
 * @brief Calcula el porcentaje de batería a partir del voltaje (método alternativo)
 * @param voltage Voltaje en voltios
 * @return Porcentaje estimado (0-100)
 */
float battery_get_percentage_from_voltage(float voltage)
{
    // Curva de descarga típica para batería LiPo 3.7V
    // Valores aproximados: 4.2V = 100%, 3.7V = 50%, 3.3V = 10%, 3.0V = 0%
    const float v_min = 3.0f;
    const float v_max = 4.2f;

    if (voltage >= v_max)
        return 100.0f;
    if (voltage <= v_min)
        return 0.0f;

    // Conversión no lineal (aproximación)
    float percent = (voltage - v_min) / (v_max - v_min) * 100.0f;

    // Corrección para hacerla más realista
    if (voltage < 3.7f)
    {
        percent = percent * 0.7f; // Zona de descarga rápida
    }

    return fmaxf(0.0f, fminf(100.0f, percent));
}

// ============================================================================
// TAREA DE MONITOREO DE BATERÍA
// ============================================================================
void battery_monitor_task(void *pvParameters)
{
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t frequency = pdMS_TO_TICKS(1000); // Actualizar cada 1s

    // Variables locales de la tarea
    esp_err_t ret;
    uint8_t alert_count = 0;
    uint8_t consecutive_errors = 0;
    const uint8_t MAX_CONSECUTIVE_ERRORS = 5;

    ESP_LOGI(TAG, "Tarea de monitoreo de batería iniciada");

    while (1)
    {
        // Intentar leer el sensor
        ret = max17043_read_all(&battery);

        if (ret == ESP_OK)
        {
            // Resetear contador de errores
            consecutive_errors = 0;

            // Mostrar estado de la batería (solo si hay cambios significativos)
            static float last_voltage = -1.0f;
            static float last_soc = -1.0f;

            if (fabsf(battery.voltage - last_voltage) > 0.01f ||
                fabsf(battery.soc - last_soc) > 0.5f)
            {
                if (battery.battery_present)
                {
                    ESP_LOGI(TAG, "Batería: %.3fV | SOC: %.1f%% | Alerta: %s",
                             battery.voltage,
                             battery.soc,
                             battery.alert ? "ACTIVA" : "OK");
                }
                else
                {
                    ESP_LOGW(TAG, "Batería: DESCONECTADA (%.3fV)", battery.voltage);
                }
                last_voltage = battery.voltage;
                last_soc = battery.soc;
            }

            // Manejar alerta de batería baja
            if (battery.alert && battery.battery_present)
            {
                alert_count++;
                if (alert_count == 1)
                {
                    ESP_LOGW(TAG, "¡ALERTA DE BATERÍA BAJA! Voltaje: %.3fV, SOC: %.1f%%",
                             battery.voltage, battery.soc);

                    // Aquí puedes agregar acciones:
                    // - Encender un LED
                    // - Enviar notificación
                    // - Guardar datos críticos
                    // - Cambiar a modo de bajo consumo
                }
                else if (alert_count > 10)
                {
                    // Si la alerta persiste por más de 10 segundos, algo puede estar mal
                    ESP_LOGW(TAG, "Alerta persistente por %d segundos", alert_count);
                }

                // Limpiar alerta para que se pueda volver a activar
                ret = max17043_clear_alert(&battery);
                if (ret != ESP_OK)
                {
                    ESP_LOGE(TAG, "Error limpiando alerta: %s", esp_err_to_name(ret));
                }
            }
            else
            {
                alert_count = 0;
            }
        }
        else
        {
            consecutive_errors++;
            ESP_LOGW(TAG, "Error leyendo MAX17043 (%d/%d): %s",
                     consecutive_errors, MAX_CONSECUTIVE_ERRORS, esp_err_to_name(ret));

            if (consecutive_errors >= MAX_CONSECUTIVE_ERRORS)
            {
                ESP_LOGE(TAG, "Demasiados errores consecutivos. Intentando reiniciar sensor...");

                // Intentar recuperar el sensor
                ret = max17043_reset(&battery);
                if (ret == ESP_OK)
                {
                    vTaskDelay(pdMS_TO_TICKS(100));
                    max17043_quick_start(&battery);
                    ESP_LOGI(TAG, "Sensor reiniciado");
                }
                else
                {
                    ESP_LOGE(TAG, "No se pudo reiniciar el sensor");
                }
                consecutive_errors = 0;
            }
        }

        // Esperar hasta la próxima lectura
        vTaskDelayUntil(&last_wake_time, frequency);
    }
}

// ============================================================================
// FUNCIÓN DE MONITOREO (appBattery)
// ============================================================================

esp_err_t appBattery(void)
{
    esp_err_t ret;

    // Prevenir inicialización múltiple
    if (s_battery_initialized)
    {
        ESP_LOGW(TAG, "Sistema de batería ya inicializado");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Inicializando sistema de batería...");

    // 1. Inicializar I2C
    ret = i2c0_master_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "No se pudo inicializar I2C0 (%s)", esp_err_to_name(ret));
        return ret;
    }

    // 2. Escanear bus I2C para verificar dispositivos (opcional)
    helper_i2c_scan(&i2c_bus0);

    // 3. Inicializar sensor
    ret = max17043_init(&i2c_bus0, &battery);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "No se pudo inicializar el sensor (%s)", esp_err_to_name(ret));
        return ret;
    }

    // 4. Configurar umbral de alerta al 10%
    ret = max17043_set_alert_threshold(&battery, 10);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "No se pudo configurar el umbral (%s)", esp_err_to_name(ret));
        // No fallamos, el sensor puede funcionar sin alerta
    }

    // 5. Ejecutar diagnóstico inicial
    max17043_diagnostic(&battery);

    // 6. Crear tarea de monitoreo de batería (prioridad baja para no interferir)
    ret = xTaskCreate(
        battery_monitor_task, // Función de la tarea
        "battery_monitor",    // Nombre
        4096,                 // Stack size (aumentado para más logging)
        NULL,                 // Parámetros
        5,                    // Prioridad (baja)
        NULL                  // Handle (no necesario)
    );

    if (ret != pdPASS)
    {
        ESP_LOGE(TAG, "No se pudo crear la tarea de monitoreo de batería");
        return ESP_ERR_NO_MEM;
    }

    s_battery_initialized = true;
    ESP_LOGI(TAG, "Sistema de batería inicializado correctamente");

    return ESP_OK;
}