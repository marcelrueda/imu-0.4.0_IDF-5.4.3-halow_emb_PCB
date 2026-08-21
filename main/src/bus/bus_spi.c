// bus_spi.c
// MODULO: Abstracción del bus SPI para múltiples dispositivos
#include "bus/bus_spi.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "SPI_BUS";
// Inicializar bus SPI

esp_err_t spi_bus_init(spi_host_device_t host) {
  spi_bus_config_t buscfg = {
      .mosi_io_num = SPI_MOSI,
      .miso_io_num = SPI_MISO,
      .sclk_io_num = SPI_SCLK,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = 64, // ajusta según payload
  };

  esp_err_t ret = spi_bus_initialize(host, &buscfg, SPI_DMA_CH_AUTO);
  if (ret == ESP_ERR_INVALID_STATE) {
    ESP_LOGW(TAG, "Bus SPI host=%d ya inicializado", host);
    return ESP_OK;
  }
  ESP_LOGI(TAG, "Bus SPI host=%d inicializado", host);
  return ret;
}

// Agregar un dispositivo al bus
esp_err_t spi_bus_add_dev(spi_host_device_t host, int cs_pin,
                          int clock_speed_hz, spi_device_handle_t *handle) {
  spi_device_interface_config_t devcfg = {
      .clock_speed_hz = clock_speed_hz,
      .mode = 0,
      .spics_io_num = cs_pin,
      .queue_size = 1,
      //.flags = SPI_DEVICE_HALFDUPLEX,
  };

  esp_err_t ret =
      spi_bus_add_device(host, &devcfg, handle); // aquí sí usamos la oficial
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Dispositivo agregado CS=%d @ %dHz", cs_pin, clock_speed_hz);
  }
  return ret;
}