// imu_tools.h

#ifndef IMU_TOOLS_H
#define IMU_TOOLS_H

#include "imu.h"
#include <stdbool.h>
extern volatile bool seismicEvent; // declaración global accesible

// ---------------------- Configuración de Ventanas ----------------------

// ---------------------- API Pública ----------------------

/**
 * @brief Calcula el RMS (Root Mean Square) de un buffer de datos.
 *
 * @param buffer Puntero al arreglo de floats
 * @param len    Longitud del arreglo
 * @return Valor RMS del buffer
 */
float compute_rms(float *buffer, int len);

/**
 * @brief Procesa una muestra del IMU y evalúa si ocurre un evento sísmico.
 *
 * @param sample Estructura de datos IMU (aceleraciones en X, Y, Z)
 */
void check_for_seismic_event(data_imu_t sample);

#endif // IMU_TOOLS_H
