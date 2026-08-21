// utils.h

#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LED_GPIO 14     // GPIO para el LED de estado, Led Verde
#define RGB_GPIO 33     // GPIO para el LED RGB, Led RGB
#define SW1_GPIO 0      // 18     // GPIO para el botón SW1
#define MM_RESET_GPIO 7 // GPIO conectado a MM_RESET_N

    extern volatile uint8_t currentPage;
    void init_SW1(void); // Inicializa el botón SW1

    // Rset físico al Morse
    void morse_reset_init(void);
    void morse_reset_pulse(void);

    // Info del dispositivo
    extern char device_id[13]; // ID del dispositivo
    void get_device_id(char *device_id_buf, size_t buf_len);

    // Helper para crear tareas con chequeo
    BaseType_t create_task(TaskFunction_t task, const char *name, uint32_t stack, void *param,
                           UBaseType_t prio, TaskHandle_t *handle,
                           int core_id); // -1 (tskNO_AFFINITY) para libre, 0 o 1 para fijar core
    void fatal_error(const char *msg);
    // RGB,LED de estado
    void init_status_led(void);
    void start_led_toggle_task(void);
    void init_status_rgb(void);                             // Inicializa el LED RGB
    void set_status_color(uint8_t r, uint8_t g, uint8_t b); // Color RGB directo
    void set_status_mode_data_collection(void);             // Violeta
    void set_status_mode_data_sending(void);                // Amarillo
    void set_status_mode_error(void);                       // Rojo
    void turn_off_status_led(void);                         // Apaga LED
#ifdef __cplusplus
}
#endif
