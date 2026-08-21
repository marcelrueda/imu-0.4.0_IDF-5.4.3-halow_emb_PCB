#ifndef DEVICE_SCL3400_H
#define DEVICE_SCL3400_H

#include "driver/spi_master.h"
#include "esp_err.h"
#include <stdbool.h>

// =============== Comandos con CRC precalculado (Datasheet + Arduino driver) ===============
#define SCL3400_CMD_SWRESET 0xB4002098    // Soft reset
#define SCL3400_CMD_CHGMODE_A 0xB400001F  // Mode A (±0.5g, 10 Hz)
#define SCL3400_CMD_CHGMODE_B 0xB4000338  // Mode B (±1.1g, 40 Hz)
#define SCL3400_CMD_RDSTAT_SUM 0x180000E5 // Read Status Summary
#define SCL3400_CMD_RDWHOAMI 0x40000000   // WHOAMI (sin CRC → requiere doble lectura)
#define SCL3400_CMD_RDACC_X 0x040000F7    // Read Acc X
#define SCL3400_CMD_RDACC_Y 0x080000FD    // Read Acc Y
#define SCL3400_CMD_RDTEMP 0x140000F9     // Read Temp
#define SCL3400_CMD_SWTCHBNK0 0xfc000073  // Switch to Bank 0
#define SCL3400_CMD_SWTCHBNK1 0xfc00016e  // Switch to Bank 1
#define SCL3400_CMD_RDSER1 0x640000a7     // Read Serial Number part 1
#define SCL3400_CMD_RDSER2 0x680000AD     // Read Serial Number part 2
#define RdTemp 0x140000ef
#define RdErrFlg1 0x1c0000e3
#define RdErrFlg2 0x200000c1
#define RdCMD 0x340000df
#define SetPwrDwn 0xb400046b
#define WakeUp 0xb400001f
#define RdWHOAMI 0x40000091
#define RdCurBank 0x7c0000b3

#define RAD_TO_DEG (180.0 / M_PI) // Conversion factor from radians to degrees
#define SCL3400_WHOAMI_VALUE 0xE0 // WHOAMI esperado según datasheet
typedef struct
{
    float x_g;
    float y_g;
    float temp_c;
    float pitch_deg;
    float roll_deg;
    float tilt_deg;
} scl3400_measurements_t;

typedef struct
{
    float x_g;    // aceleración eje X en g
    float y_g;    // aceleración eje Y en g
    float temp_c; // temperatura en °C
} scl3400_data_t;

typedef enum
{
    SCL3400_MODE_A = 0, // ±30°, 10Hz LPF, 32768 LSB/g
    SCL3400_MODE_B = 1  // ±90°, 40Hz LPF, 16384 LSB/g
} scl3400_mode_t;

// ====================== API ======================
spi_device_handle_t SCL3400_get_handle(void);                                          // Exponer el handle SPI
esp_err_t scl3400_init(spi_device_handle_t *out_handle, int cs_gpio);                  // Inicialización
bool scl3400_check_whoami(spi_device_handle_t spi);                                    // Verifica WHOAMI
bool scl3400_read_all(spi_device_handle_t spi, float *x_g, float *y_g, float *temp_c); // Lectura de datos
float scl3400_calculate_angle(float accel_g);                                          // Calcula ángulo en grados a partir de aceleración en g
uint32_t scl3400_get_serial(spi_device_handle_t spi);                                  // Obtiene número de serie

bool scl3400_read_all_noCRC(spi_device_handle_t spi, float *x_g, float *y_g, float *temp_c) ; // Lectura sin CRC (no recomendado)
bool scl3400_read_all_with_angles(spi_device_handle_t spi, scl3400_measurements_t *out);
void app_SCL3400(void);
static uint32_t scl3400_transfer(spi_device_handle_t spi, uint32_t cmd);
esp_err_t scl3400_set_mode(spi_device_handle_t spi, scl3400_mode_t mode);
esp_err_t scl3400_read_status(spi_device_handle_t spi);
void scl3400_setup();
#endif // DEVICE_SCL3400_H

/*

Los RS bits son “status bits” del sensor que indican el estado de la lectura. Según el datasheet del SCL3400:
0x00 = OK
0x01 = Busy / procesando
0x02 = Warning menor
0x03 = Error crítico o lectura inválida


*/