// main.h

#ifndef MAIN_H
#define MAIN_H

#include <esp_log.h>

#define VER_SOFTWARE "v1.30.00"           // Versión del software
#define VER_HARDWARE "v1.20.00"           // Versión del hardware
#define manufacturer "Lycans Electronics" // Fabricante
#define yearfactory 25                    // Año de fabricación
#define internalver 13                    // Versión del producto
void mqtt_live_task(void *arg);
void app_main(void);

#endif