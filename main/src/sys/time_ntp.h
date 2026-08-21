// time_ntp.h
#ifndef TIME_NTP_H
#define TIME_NTP_H

#include <stdbool.h>

// Inicia la tarea de sincronización automática de hora UTC cada hora
void start_time_sync_task(void);

// Configura el sistema para usar la hora local de un país (TZ)
void set_system_country_time(const char *country);

// Imprime la hora local según país (solo para logging)
void print_country_time(const char *country);

// Actualiza la hora del sistema desde un servidor NTP
void time_sync_task(void *pvParameters);
#endif // TIME_NTP_H
