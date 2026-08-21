#include "helper_i2c.h"
#include "bus/bus_i2c.h"

// bus I2C maestro
HelperI2C i2c_bus0; // Bus I2C0
HelperI2C i2c_bus1; // Bus I2C1
static const char *TAG = "BUS_I2C";
// Inicializar el bus I2C0 maestro

esp_err_t i2c0_master_init(void)
{
    static bool initialized = false; // Seguridad extra por si llaman varias veces

    if (initialized)
    {
        ESP_LOGW(TAG, "i2c0_master_init(): I2C0 ya estaba inicializado, omitiendo");
        return ESP_OK;
    }

    esp_err_t ret = helper_i2c_init(&i2c_bus0, I2C_NUM_0, I2C0_SCL, I2C0_SDA, I2C0_FREQ_HZ);
    if (ret == ESP_OK)
    {
        initialized = true;
    }
    return ret;
}
// Inicializar el bus I2C1 maestro
void i2c1_master_init(void)
{
    helper_i2c_init(&i2c_bus1, I2C_NUM_1, I2C1_SCL, I2C1_SDA, I2C1_FREQ_HZ);
}
