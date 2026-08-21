// device_display.h
#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdio.h>             // Incluir la biblioteca estándar de entrada/salida
#include <string.h>            // Incluir la biblioteca de manipulación de cadenas
#include "bus/bus_i2c.h"           // Incluir el archivo de bus I2C
#include "ssd1306.h"           // Incluir el archivo de la biblioteca SSD1306
#include "esp_log.h"           // Incluir la biblioteca de registro de ESP-IDF
#include "freertos/FreeRTOS.h" // Incluir FreeRTOS para tareas y temporizadores
#include "freertos/task.h"     // Incluir FreeRTOS para tareas y temporizadores

void testdrawcircle(void);
void testfillrect(void);
void testdrawtriangle(void);
void testfilltriangle(void);
void testdrawroundrect(void);
void testfillroundrect(void);
void testdrawrect(void);
void testdrawline(void);
void testscrolltext(void);
void appDisplay(void);
void SSD1306_drawPercentbar(int x, int y, int width, int height, int progress);
void display_show_loading(int progress);

void display_task(void *pvParameters);

#endif /* DISPLAY_H */