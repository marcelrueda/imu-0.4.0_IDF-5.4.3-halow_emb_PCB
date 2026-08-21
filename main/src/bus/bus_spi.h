// bus_spi.h
// MODULO: Abstracción del bus SPI para múltiples dispositivos
#ifndef SPI_BUS_H
#define SPI_BUS_H

#include "driver/spi_master.h"
#include "esp_err.h"

// Pines compartidos del bus SPI3
#define SPI_MOSI 41
#define SPI_MISO 42
#define SPI_SCLK 40

// Inicializar bus SPI
esp_err_t spi_bus_init(spi_host_device_t host);

// Agregar un dispositivo al bus
esp_err_t spi_bus_add_dev(spi_host_device_t host, int cs_pin, int clock_speed_hz, spi_device_handle_t *handle);

#endif // SPI_BUS_H
