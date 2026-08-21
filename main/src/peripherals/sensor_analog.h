/**
 * @file sensor_analog.h
 * @version 1.00
 * @brief API para lectura de voltaje de batería en ESP32-S3
 *
 * Usa divisor resistivo 33k/10k conectado a IO5 (ADC1_CH4).
 */

#ifndef SENSOR_ANALOG_H
#define SENSOR_ANALOG_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>
    extern float vbatt_filtered;

    /**
     * @brief Inicializa el ADC en IO5 (ADC1_CH4).
     */
    void analog_input_init(void);
    /**
     * @brief Realiza un diagnóstico del ADC imprimiendo varias lecturas RAW.
     */
    void analog_input_diagnostic(void);
    /**
     * @brief Lee el voltaje de batería usando el divisor resistivo.
     *
     * @return Voltaje en la batería [V].
     */
    float analog_input_read_battery(void);

    /**
     * @brief Convierte el voltaje de batería a porcentaje de carga.
     *
     * @param voltage Voltaje en la batería [V].
     * @return Porcentaje de carga (0-100%).
     */
    int battery_get_percentage(float v);

    /**
     * @brief Crea una tarea periódica que imprime el voltaje de batería.
     */
    void appADC(void);

#ifdef __cplusplus
}
#endif

#endif // SENSOR_ANALOG_H
