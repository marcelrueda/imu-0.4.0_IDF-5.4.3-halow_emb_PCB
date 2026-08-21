// device_SCL3400.c
// MODULO: Driver  para el sensor SCL3400-D01  (Inclinometro) vía SPI

// Autor: Carlos A Vargas
// carlos_vargas_c@outlook.com
// Versión: 0.1 - Agosto 2024
#include <inttypes.h>
#include "bus/bus_spi.h"
#include "device_SCL3400.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "freertos/task.h"
#include <math.h> // ¡IMPORTANTE! Necesario para asinf() y M_PI

static const char *TAG = "SCL3400";            // etiqueta para logs
static spi_device_handle_t spi_handle_SCL3400; // handle SPI global
#define SCL3400_PIN_CS 38                      // CS en tu hardware (cámbialo si usas otro pin)

// ================= Exponer el handle SPI =================
spi_device_handle_t SCL3400_get_handle(void)
{
    return spi_handle_SCL3400;
}
// ================= CRC Helper =================
static uint8_t scl3400_calculate_crc(uint32_t frame)
{
    uint8_t crc = 0xFF;
    for (int bit = 31; bit > 7; bit--)
    {
        uint8_t bitval = (frame >> bit) & 0x01;
        uint8_t temp = (crc & 0x80);
        if (bitval == 1)
        {
            temp ^= 0x80;
        }
        crc <<= 1;
        if (temp)
        {
            crc ^= 0x1D;
        }
    }
    return ~crc;
}
// ================= SPI Helper con CRC opcional =================
static bool scl3400_transfer_with_crc(spi_device_handle_t spi, uint32_t cmd, uint16_t *data, bool check_crc)
{
    const int MAX_RETRIES = 3;

    for (int attempt = 0; attempt < MAX_RETRIES; attempt++)
    {
        // Primera transferencia (enviar comando)
        uint32_t resp1 = scl3400_transfer(spi, cmd);
        if (resp1 == 0xFFFFFFFF)
            continue; // reintentar

        // Segunda transferencia (leer respuesta)
        uint32_t resp2 = scl3400_transfer(spi, cmd);
        if (resp2 == 0xFFFFFFFF)
            continue; // reintentar

        // Para comandos que no requieren CRC (como WHOAMI)
        if (!check_crc)
        {
            *data = (resp2 >> 8) & 0xFFFF;
            return true;
        }

        // Verificar CRC
        uint8_t resp_crc = resp2 & 0xFF;
        uint8_t calc_crc = scl3400_calculate_crc(resp2);

        if (resp_crc != calc_crc)
        {
            ESP_LOGW(TAG, "CRC FAIL (try %d/%d): got 0x%02X, expected 0x%02X",
                     attempt + 1, MAX_RETRIES, resp_crc, calc_crc);
            vTaskDelay(pdMS_TO_TICKS(2));
            continue; // reintentar
        }

        // Verificar RS bits
        uint8_t rs_bits = (resp2 >> 23) & 0x03;
        if (rs_bits == 0x03)
        {
            ESP_LOGW(TAG, "RS bits invalid=0x%02X (try %d/%d)", rs_bits, attempt + 1, MAX_RETRIES);
            vTaskDelay(pdMS_TO_TICKS(2));
            continue; // reintentar
        }

        // Si llega aquí, la lectura es válida
        *data = (resp2 >> 8) & 0xFFFF;
        return true;
    }

    ESP_LOGW(TAG, "Fallo tras %d intentos en comando 0x%08X", MAX_RETRIES, (unsigned int)cmd);
    return false;
}

// ================= SPI Helper =================
static uint32_t scl3400_transfer(spi_device_handle_t spi, uint32_t cmd)
{
    uint8_t tx[4] = {
        (cmd >> 24) & 0xFF,
        (cmd >> 16) & 0xFF,
        (cmd >> 8) & 0xFF,
        cmd & 0xFF};
    uint8_t rx[4] = {0};

    spi_transaction_t t = {
        .length = 32,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };

    esp_err_t ret = spi_device_transmit(spi, &t);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "SPI TX error: %s", esp_err_to_name(ret));
        return 0xFFFFFFFF;
    }

    uint32_t resp = (rx[0] << 24) | (rx[1] << 16) | (rx[2] << 8) | rx[3];
    vTaskDelay(pdMS_TO_TICKS(1)); // ≥10 µs entre transacciones
    return resp;
}
// ================= Conversión física =================
static float convert_accel(int16_t raw)
{
    return (float)raw / 32768.0f; // Mode A 32768,     Mode B 16384    sensitivity
}
static float convert_temp(int16_t raw)
{
    return -273.0f + ((float)raw / 18.9f);
}
float scl3400_calculate_angle(float accel_g)
{
    if (accel_g > 1.0f)
        accel_g = 1.0f;
    if (accel_g < -1.0f)
        accel_g = -1.0f;
    float rad = asinf(accel_g);
    return rad * (180.0f / M_PI);
}

// ================= Inicialización =================
esp_err_t scl3400_init(spi_device_handle_t *out_handle, int cs_gpio)
{
    esp_err_t ret;
    spi_device_handle_t spi;

    // Registrar el dispositivo en el bus ya inicializado
    ret = spi_bus_add_dev(SPI3_HOST, cs_gpio, 1 * 1000 * 1000, &spi); // 1 MHz seguro
    if (ret != ESP_OK)
        return ret;

    *out_handle = spi;

    // === Secuencia de arranque (Arduino driver) ===
    vTaskDelay(pdMS_TO_TICKS(10)); // Power-up delay

    scl3400_transfer(spi, SCL3400_CMD_SWRESET);
    vTaskDelay(pdMS_TO_TICKS(50));

    scl3400_transfer(spi, SCL3400_CMD_CHGMODE_A);
    vTaskDelay(pdMS_TO_TICKS(200));

    // Limpiar STATUS (leer varias veces)
    scl3400_transfer(spi, SCL3400_CMD_RDSTAT_SUM);
    scl3400_transfer(spi, SCL3400_CMD_RDSTAT_SUM);
    scl3400_transfer(spi, SCL3400_CMD_RDSTAT_SUM);

    // WHOAMI doble lectura
    scl3400_transfer(spi, SCL3400_CMD_RDWHOAMI);
    uint32_t resp = scl3400_transfer(spi, SCL3400_CMD_RDWHOAMI);
    uint8_t whoami = (resp >> 8) & 0xFF;
    ESP_LOGI(TAG, "WHOAMI=0x%02X", whoami);

    if (whoami != SCL3400_WHOAMI_VALUE)
    {
        ESP_LOGE(TAG, "SCL3400 no detectado (WHOAMI=0x%02X)", whoami);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "SCL3400 inicializado OK");
    return ESP_OK;
}

// ================= Lecturas =================
// Función de debug  CRC + RS
static void scl3400_debug_crc(spi_device_handle_t spi)
{
    const struct
    {
        const char *name;
        uint32_t cmd;
    } tests[] = {
        {"X", SCL3400_CMD_RDACC_X},
        {"Y", SCL3400_CMD_RDACC_Y},
        {"Temp", SCL3400_CMD_RDTEMP},
    };

    for (int i = 0; i < 3; i++)
    {
        // Dummy + lectura real
        scl3400_transfer(spi, tests[i].cmd);
        uint32_t resp = scl3400_transfer(spi, tests[i].cmd);

        uint16_t raw = (resp >> 8) & 0xFFFF;                         // datos puros
        uint8_t rs_bits = (resp >> 23) & 0x03;                       // status bits
        uint8_t resp_crc = resp & 0xFF;                              // CRC del frame
        uint8_t calc_crc = scl3400_calculate_crc(resp & 0xFFFFFF00); // <-- solo 24 bits válidos

        ESP_LOGI(TAG, "[%s] RAW=0x%08X | DATA=0x%04X | RS=0x%02X | CRC=0x%02X | CALC=0x%02X",
                 tests[i].name, (unsigned int)resp, raw, rs_bits, resp_crc, calc_crc);

        if (resp_crc == calc_crc)
            ESP_LOGI(TAG, "[%s] CRC OK", tests[i].name);
        else
            ESP_LOGW(TAG, "[%s] CRC FAIL", tests[i].name);

        esp_rom_delay_us(500); // medio ms entre lecturas
    }
}
// ================= WHOAMI =================
bool scl3400_check_whoami(spi_device_handle_t spi)
{
    uint16_t whoami_data;
    if (!scl3400_transfer_with_crc(spi, SCL3400_CMD_RDWHOAMI, &whoami_data, false))
    {
        return false;
    }
    return (whoami_data & 0xFF) == SCL3400_WHOAMI_VALUE;
}
// ================= Lectura con CRC y validación =================
bool scl3400_read_all(spi_device_handle_t spi, float *x_g, float *y_g, float *temp_c)
{
    struct
    {
        const char *name;
        uint32_t cmd;
        uint16_t raw;
    } sensors[3] = {
        {"X", SCL3400_CMD_RDACC_X, 0},
        {"Y", SCL3400_CMD_RDACC_Y, 0},
        {"Temp", SCL3400_CMD_RDTEMP, 0},
    };

    for (int i = 0; i < 3; i++)
    {
        // Dummy + real
        scl3400_transfer(spi, sensors[i].cmd);
        esp_rom_delay_us(10); // espera mínima (5–10 µs suelen bastar)
        uint32_t resp = scl3400_transfer(spi, sensors[i].cmd);

        uint16_t raw = (resp >> 8) & 0xFFFF;
        uint8_t rs_bits = (resp >> 23) & 0x03;
        uint8_t resp_crc = resp & 0xFF;
        uint8_t calc_crc = scl3400_calculate_crc(resp & 0xFFFFFF00);

        // ESP_LOGI(TAG, "[%s] RAW=0x%08X | DATA=0x%04X | RS=0x%02X | CRC=0x%02X | CALC=0x%02X",
        //          sensors[i].name, (unsigned int)resp, raw, rs_bits, resp_crc, calc_crc);

        if (resp_crc != calc_crc)
        {
            ESP_LOGW(TAG, "[%s] CRC mismatch (got=0x%02X, expected=0x%02X)", sensors[i].name, resp_crc, calc_crc);
            return false;
        }
        if (rs_bits == 0x03)
        {
            ESP_LOGW(TAG, "[%s] RS bits invalid=0x%02X", sensors[i].name, rs_bits);
            return false;
        }

        sensors[i].raw = raw;
        // ESP_LOGI(TAG, "[%s] OK", sensors[i].name);

        vTaskDelay(pdMS_TO_TICKS(10)); // Espera larga para que el siguiente eje esté listo
    }

    // Conversión física
    *x_g = convert_accel((int16_t)sensors[0].raw);
    *y_g = convert_accel((int16_t)sensors[1].raw);
    *temp_c = convert_temp((int16_t)sensors[2].raw);

    return true;
}

bool scl3400_read_all_noCRC(spi_device_handle_t spi, float *x_g, float *y_g, float *temp_c)
{
    if (!x_g || !y_g || !temp_c)
        return false;

    struct
    {
        const char *name;
        uint32_t cmd;
        int16_t raw;
    } sensors[3] = {
        {"X", SCL3400_CMD_RDACC_X, 0},
        {"Y", SCL3400_CMD_RDACC_Y, 0},
        {"Temp", SCL3400_CMD_RDTEMP, 0},
    };

    for (int i = 0; i < 3; i++)
    {
        // Dummy + real (igual que antes)
        scl3400_transfer(spi, sensors[i].cmd);
        esp_rom_delay_us(10); // espera corta entre dummy y real
        uint32_t resp = scl3400_transfer(spi, sensors[i].cmd);

        if (resp == 0xFFFFFFFF)
        {
            ESP_LOGW(TAG, "[%s] fallo en transferencia SPI", sensors[i].name);
            return false;
        }

        sensors[i].raw = (resp >> 8) & 0xFFFF;
        vTaskDelay(pdMS_TO_TICKS(10)); // 🔹 Delay largo antes de leer el siguiente eje
    }

    // Conversión física
    *x_g = convert_accel(sensors[0].raw);
    *y_g = convert_accel(sensors[1].raw);
    *temp_c = convert_temp(sensors[2].raw);

    return true;
}

// ================= Calcular inclinación total =================
float scl3400_get_inclination_deg(float x_g, float y_g)
{
    // Magnitud en el plano XY
    float tilt_g = sqrtf(x_g * x_g + y_g * y_g);

    // Limitar por seguridad al rango [-1,1]
    if (tilt_g > 1.0f)
        tilt_g = 1.0f;
    if (tilt_g < -1.0f)
        tilt_g = -1.0f;

    // Convertir a grados
    float tilt_rad = asinf(tilt_g);
    return tilt_rad * (180.0f / M_PI);
}

// ================= Lectura con ángulos y sin validar CRC =================
bool scl3400_read_all_with_angles(spi_device_handle_t spi, scl3400_measurements_t *out)
{
    if (!out)
        return false;

    // Leer X
    scl3400_transfer(spi, SCL3400_CMD_RDACC_X);
    uint32_t rx = scl3400_transfer(spi, SCL3400_CMD_RDACC_X);
    if (rx == 0xFFFFFFFF)
        return false;
    int16_t raw_x = (rx >> 8) & 0xFFFF;

    // Leer Y
    scl3400_transfer(spi, SCL3400_CMD_RDACC_Y);
    uint32_t ry = scl3400_transfer(spi, SCL3400_CMD_RDACC_Y);
    if (ry == 0xFFFFFFFF)
        return false;
    int16_t raw_y = (ry >> 8) & 0xFFFF;

    // Leer Temp
    scl3400_transfer(spi, SCL3400_CMD_RDTEMP);
    uint32_t rt = scl3400_transfer(spi, SCL3400_CMD_RDTEMP);
    if (rt == 0xFFFFFFFF)
        return false;
    int16_t raw_t = (rt >> 8) & 0xFFFF;

    // Conversión física
    float x_g = convert_accel(raw_x);
    float y_g = convert_accel(raw_y);
    float temp_c = convert_temp(raw_t);

    // Pitch = asin(x_g) (inclinación adelante atrás)
    double pitch_rad = asin(fmaxf(fminf(x_g, 1.0f), -1.0f));
    float pitch_deg = pitch_rad * RAD_TO_DEG;

    // Roll = –asin(y_g) (inclinación lateral)
    double roll_rad = -asin(fmaxf(fminf(y_g, 1.0f), -1.0f));
    float roll_deg = roll_rad * RAD_TO_DEG;

    // Tilt genérico: inclinación combinada en el plano X-Y
    float tilt_g = sqrtf(x_g * x_g + y_g * y_g);
    double tilt_rad = asin(fminf(tilt_g, 1.0f));
    float tilt_deg = tilt_rad * RAD_TO_DEG;

    // Llenar salida
    out->x_g = x_g;
    out->y_g = y_g;
    out->temp_c = temp_c;
    out->pitch_deg = pitch_deg;
    out->roll_deg = roll_deg;
    out->tilt_deg = tilt_deg;

    return true;
}
// ================= Número de serie =================
uint32_t scl3400_get_serial(spi_device_handle_t spi)
{
    // Cambiar a Bank1
    scl3400_transfer(spi, SCL3400_CMD_SWTCHBNK1);

    // Leer parte baja
    scl3400_transfer(spi, SCL3400_CMD_RDSER1); // dummy
    uint32_t resp1 = scl3400_transfer(spi, SCL3400_CMD_RDSER1);

    // Leer parte alta
    scl3400_transfer(spi, SCL3400_CMD_RDSER2); // dummy
    uint32_t resp2 = scl3400_transfer(spi, SCL3400_CMD_RDSER2);

    // Volver a Bank0
    scl3400_transfer(spi, SCL3400_CMD_SWTCHBNK0);

    // Concatenar 16 bits válidos de cada respuesta
    uint32_t serial = ((resp2 >> 8) & 0xFFFF) << 16 | ((resp1 >> 8) & 0xFFFF);

    if (serial == 0)
    {
        ESP_LOGW(TAG, "Error leyendo Serial (resp1=0x%08X, resp2=0x%08X)", (unsigned int)resp1, (unsigned int)resp2);
    }
    else
    {
        ESP_LOGI(TAG, "Serial Number: 0x%08X", (unsigned int)serial);
    }

    return serial;
}
// ================= Prueba completa =================
void scl3400_setup()
{
    ESP_LOGI(TAG, "Iniciando prueba del SCL3400...");

    if (scl3400_init(&spi_handle_SCL3400, SCL3400_PIN_CS) != ESP_OK)
    {
        ESP_LOGE(TAG, "Error inicializando el sensor SCL3400");
        return;
    }

    if (!scl3400_check_whoami(spi_handle_SCL3400))
    {
        ESP_LOGE(TAG, "SCL3400 no responde correctamente al WHOAMI");
        return;
    }

    ESP_LOGI(TAG, "Testing CRC...");
    scl3400_debug_crc(spi_handle_SCL3400);
    ESP_LOGI(TAG, "SCL3400 listo para lecturas");
    uint32_t serial = scl3400_get_serial(spi_handle_SCL3400);
    ESP_LOGI(TAG, "Serial leído: 0x%08X", (unsigned int)serial);
}

void scl3400_read_all_validationCRC()
{
}

// ================= Función de prueba =================
void app_SCL3400(void)
{
    scl3400_setup();

    while (1)
    {
        float x_g, y_g, temp_c;
        ESP_LOGI(TAG, "Con CRC...");
        if (scl3400_read_all(spi_handle_SCL3400, &x_g, &y_g, &temp_c))
        {
            float angle_x = scl3400_calculate_angle(x_g);
            float angle_y = scl3400_calculate_angle(y_g);
            ESP_LOGI(TAG, "X: %.4f g (%.2f°), Y: %.4f g (%.2f°), Temp: %.2f °C", x_g, angle_x, y_g, angle_y, temp_c);
        }
        else
        {
            ESP_LOGW(TAG, "Fallo en lectura de datos del SCL3400");
        }

        vTaskDelay(pdMS_TO_TICKS(100)); // 10 Hz de muestreo

        scl3400_measurements_t meas;
        ESP_LOGI(TAG, "Sin CRC...");
        if (scl3400_read_all_with_angles(spi_handle_SCL3400, &meas))
        {
            ESP_LOGI(TAG,
                     "X=%.3fg, Y=%.3fg, Temp=%.2f°C, Pitch=%.2f°, Roll=%.2f°, Tilt=%.2f°",
                     meas.x_g, meas.y_g, meas.temp_c, meas.pitch_deg, meas.roll_deg, meas.tilt_deg);
        }
        else
        {
            ESP_LOGW(TAG, "Fallo en lectura con ángulos");
        }
        break; // solo una iteración para prueba
    }
}