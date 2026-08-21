// max17043.h - Header file for the MAX17043 battery sensor driver
#ifndef MAX17043_H
#define MAX17043_H

#include <stdint.h>
#include "driver/i2c_master.h"
#include "helper_i2c.h"

#ifdef __cplusplus
extern "C"
{
#endif

// Dirección I2C del MAX17043 (fija)
#define MAX17043_I2C_ADDR 0x36

// Registros del MAX17043
#define MAX17043_REG_VCELL 0x02   // Voltaje de la celda (2 bytes, R)
#define MAX17043_REG_SOC 0x04     // Estado de carga (2 bytes, R)
#define MAX17043_REG_MODE 0x06    // Modo de operación (W) - AQUÍ va Quick-Start
#define MAX17043_REG_VERSION 0x08 // Versión del chip (R)
#define MAX17043_REG_CONFIG 0x0C  // Configuración (R/W) - RCOMP, SLEEP, ALRT, ATHD
#define MAX17043_REG_CMD 0xFE     // Comando (W) - usado SOLO para POR/Reset

// Comandos
// Quick Start SIEMPRE se escribe en MAX17043_REG_MODE (0x06), nunca en REG_CMD.
#define MAX17043_CMD_QUICK_START 0x4000
// Reset (Power-On-Reset) SIEMPRE se escribe en MAX17043_REG_CMD (0xFE).
#define MAX17043_CMD_RESET 0x5400

// Registro CONFIG (Figure 5 del datasheet):
//   bits [15:8] = RCOMP
//   bit  [7]    = SLEEP
//   bit  [6]    = reservado en MAX17043/44 (ALSC solo existe en MAX17048/49)
//   bit  [5]    = ALRT  -> flag de alerta (se limpia escribiendo 0)
//   bits [4:0]  = ATHD  -> umbral de alerta, codificado como: %% real = 32 - ATHD
//                  (ATHD=00000b -> 32%%, ATHD=11111b -> 1%%)
//
// IMPORTANTE: el bit de alerta (ALRT) vive en el registro CONFIG, NO en el
// registro SOC. El bit 5 del registro SOC es parte del valor fraccionario
// de carga (1/256%% por LSB) y no debe interpretarse como flag de alerta.
#define MAX17043_CONFIG_SLEEP_BIT (1 << 7)
#define MAX17043_ALERT_BIT (1 << 5) // Bit ALRT dentro de CONFIG (no de SOC)
#define MAX17043_ATHD_MASK 0x1F

// Umbrales de validación
#define MAX17043_VOLTAGE_MIN 2.5f
#define MAX17043_VOLTAGE_MAX 4.3f
#define MAX17043_SOC_MIN 0.0f
#define MAX17043_SOC_MAX 100.0f

    typedef struct
    {
        i2c_master_dev_handle_t dev_handle; // Manejador del dispositivo I2C
        HelperI2CDevice i2c_device;         // Información del esclavo en el bus helper
        float voltage;                      // Último voltaje leído (en V)
        float soc;                          // Último SOC leído (en %)
        uint8_t alert;                      // Estado de la alerta (1=activa, 0=inactiva)
        bool battery_present;               // Indica si se detecta batería
    } max17043_t;

    /**
     * @brief Inicializa el sensor MAX17043
     * @param bus Puntero al bus I2C ya inicializado
     * @param sensor Puntero a la estructura del sensor
     * @return ESP_OK en éxito, otro código en error
     */
    esp_err_t max17043_init(HelperI2C *bus, max17043_t *sensor);

    /**
     * @brief Lee el voltaje de la batería
     * @param sensor Puntero a la estructura del sensor
     * @param voltage Puntero donde almacenar el voltaje (en V)
     * @return ESP_OK en éxito, otro código en error
     */
    esp_err_t max17043_read_voltage(max17043_t *sensor, float *voltage);

    /**
     * @brief Lee el porcentaje de carga (SOC)
     * @param sensor Puntero a la estructura del sensor
     * @param soc Puntero donde almacenar el SOC (en %)
     * @return ESP_OK en éxito, otro código en error
     */
    esp_err_t max17043_read_soc(max17043_t *sensor, float *soc);

    /**
     * @brief Lee voltaje y SOC simultáneamente, y refresca el flag de alerta
     *        leyendo también el registro CONFIG (3 transacciones I2C en total).
     * @param sensor Puntero a la estructura del sensor
     * @return ESP_OK en éxito, otro código en error
     */
    esp_err_t max17043_read_all(max17043_t *sensor);

    /**
     * @brief Configura el umbral de alerta por batería baja
     * @param sensor Puntero a la estructura del sensor
     * @param threshold_percent Umbral en % real (1% a 32%). Internamente se
     *        convierte a ATHD = 32 - threshold_percent según el datasheet.
     * @return ESP_OK en éxito, otro código en error
     */
    esp_err_t max17043_set_alert_threshold(max17043_t *sensor, uint8_t threshold_percent);

    /**
     * @brief Lee el estado de la alerta (bit ALRT del registro CONFIG)
     * @param sensor Puntero a la estructura del sensor
     * @param alert Puntero donde almacenar el estado (1 = alerta activa)
     * @return ESP_OK en éxito, otro código en error
     */
    esp_err_t max17043_get_alert(max17043_t *sensor, uint8_t *alert);

    /**
     * @brief Limpia la alerta (limpia el bit ALRT del registro CONFIG mediante
     *        read-modify-write; NO escribe en el registro COMMAND).
     * @param sensor Puntero a la estructura del sensor
     * @return ESP_OK en éxito, otro código en error
     */
    esp_err_t max17043_clear_alert(max17043_t *sensor);

    /**
     * @brief Ejecuta una calibración rápida (Quick Start).
     *        Escribe en el registro MODE (0x06), según el datasheet.
     * @param sensor Puntero a la estructura del sensor
     * @return ESP_OK en éxito, otro código en error
     */
    esp_err_t max17043_quick_start(max17043_t *sensor);

    /**
     * @brief Pone el sensor en modo sleep (bajo consumo)
     * @param sensor Puntero a la estructura del sensor
     * @return ESP_OK en éxito, otro código en error
     */
    esp_err_t max17043_sleep(max17043_t *sensor);

    /**
     * @brief Despierta el sensor del modo sleep
     * @param sensor Puntero a la estructura del sensor
     * @return ESP_OK en éxito, otro código en error
     */
    esp_err_t max17043_wakeup(max17043_t *sensor);

    /**
     * @brief Envía un Power-On-Reset (POR) al chip escribiendo en el
     *        registro COMMAND (0xFE). El chip queda como recién energizado.
     * @param sensor Puntero a la estructura del sensor
     * @return ESP_OK en éxito, otro código en error
     */
    esp_err_t max17043_reset(max17043_t *sensor);

    /**
     * @brief Realiza un diagnóstico completo del sensor
     * @param sensor Puntero a la estructura del sensor
     */
    void max17043_diagnostic(max17043_t *sensor);

    /**
     * @brief Verifica si hay una batería conectada
     * @param sensor Puntero a la estructura del sensor
     * @return true si hay batería, false en caso contrario
     */
    bool max17043_is_battery_present(max17043_t *sensor);

    esp_err_t appBattery(void);
    
#ifdef __cplusplus
}
#endif

#endif // MAX17043_H