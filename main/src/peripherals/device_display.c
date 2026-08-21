
// device_display.c
#include "peripherals/device_display.h"
#include "peripherals/max17043.h" // MAX17043 battery monitor
#include "common/utils.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "image/imagen.h" // según la ubicación real del archivo
#include "imu/data_sensor.h"
#include "imu/imu.h"
#include "imu/imu_tools.h"
#include "main.h"
#include "net/network_selector.h"      // Selector de red
#include "peripherals/sensor_analog.h" // ADC
#include "sys/system_metrics.h"        // Métricas del sistema
#include <math.h>
#include <sys/time.h>
#include <time.h>

extern HelperI2C i2c_bus0;              // Bus I2C0 global
static HelperI2CDevice display;         // Dispositivo I2C esclavo
extern volatile data_imu_t last_sample; // Última muestra para pantalla
extern max17043_t battery;              // Sensor MAX17043 para voltaje de batería
static const char *TAG = "DISPLAY";

void appDisplay()
{
  esp_err_t ret;

  // Inicializa el bus I2C (si no lo está)
  ret = i2c0_master_init();
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "No se pudo inicializar I2C0 (%s)", esp_err_to_name(ret));
    return;
  }

  // === ESCANEO PREVIO ===
  helper_i2c_scan(&i2c_bus0);

  // Agrega el dispositivo display al bus
  ret = helper_i2c_add_device(&i2c_bus0, &display, SSD1306_I2C_ADDRESS,
                              I2C0_FREQ_HZ);
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "no se pudo agregar display al bus I2C (%s)",
             esp_err_to_name(ret));
    return;
  }

  // Inicializa el display SSD1306
  ret = SSD1306_Begin(&display, SSD1306_SWITCHCAPVCC, SSD1306_I2C_ADDRESS);
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "no se pudo iniciar el SSD1306 (%s)", esp_err_to_name(ret));
    return;
  }

  ESP_LOGI(TAG, "Display SSD1306 inicializado correctamente");
  vTaskDelay(pdMS_TO_TICKS(100)); // Tiempo de estabilización
}

void display_show_loading(int progress)
{
  SSD1306_ClearDisplay();
  // Título
  SSD1306_Drawtext(20, 10, "IMU SYSTEM 2026", 1);

  // Texto de carga
  char buf[32];
  snprintf(buf, sizeof(buf), "Cargando... %d%%", progress);
  SSD1306_Drawtext(25, 30, buf, 1);

  // Barra de progreso (x, y, w, h)
  int barW = 100;
  int barH = 8;
  int barX = (SSD1306_LCDWIDTH - barW) / 2;
  int barY = 45;

  SSD1306_DrawRect(barX, barY, barW, barH);
  int filledW = (progress * (barW - 4)) / 100;
  if (filledW > 0)
  {
    SSD1306_FillRect(barX + 2, barY + 2, filledW, barH - 4, 1);
  }

  SSD1306_Display(&display);
}

// Tasks
static int wave_x = 0;
static int prev_y = SSD1306_LCDHEIGHT / 2; // para dibujar línea continua
static bool inWaveMode = false;            // para detectar cambio de estado

void sismic_mode_display()
{
  if (!inWaveMode)
  {
    // Entramos por primera vez al modo sismógrafo
    // Limpiamos toda la pantalla y reiniciamos parámetros
    SSD1306_ClearDisplay();
    wave_x = 0;
    prev_y = SSD1306_LCDHEIGHT / 2;
    inWaveMode = true;
  }

  // No limpiamos toda la pantalla, solo columna actual
  for (int y = 0; y < SSD1306_LCDHEIGHT; y++)
    SSD1306_DrawPixel(wave_x, y, 0);

  // Escalar señal (ejemplo accY)
  float signal = last_sample.accY;
  int center = SSD1306_LCDHEIGHT / 2;
  int scale = 10;
  int y_val = center - (int)(signal * scale);

  if (y_val < 0)
    y_val = 0;
  if (y_val >= SSD1306_LCDHEIGHT)
    y_val = SSD1306_LCDHEIGHT - 1;

  // Dibujar línea continua
  SSD1306_DrawLine(wave_x - 1, prev_y, wave_x, y_val, 1);
  prev_y = y_val;

  // Avanzar X
  wave_x++;
  if (wave_x >= SSD1306_LCDWIDTH)
  {
    wave_x = 0;
    SSD1306_ClearDisplay();
  }

  SSD1306_Drawtext(0, SSD1306_LCDHEIGHT - 10, "accY", 1);
  SSD1306_Display(&display);
  vTaskDelay(pdMS_TO_TICKS(33)); // ~30 Hz
}

void info_NET_mode_display()
{
  SSD1306_ClearDisplay();

  char buffer[64];
  network_status_t st;
  network_get_status(&st);

  SSD1306_Drawtext(0, 0, st.link_up ? "LINK: UP" : "NET: DOWN", 1);
  // Línea 0: tipo de interfaz directamente
  SSD1306_Drawtext(60, 0, network_iface_to_str(st.iface), 1);

  snprintf(buffer, sizeof(buffer), "ID: %s", device_id);
  SSD1306_Drawtext(0, 10, buffer, 1);

  snprintf(buffer, sizeof(buffer), "IP: %s", st.ip);
  SSD1306_Drawtext(0, 20, buffer, 1);

  snprintf(buffer, sizeof(buffer), "GW: %s", st.gw);
  SSD1306_Drawtext(0, 30, buffer, 1);

  if (st.rssi != INT32_MIN)
  {
    snprintf(buffer, sizeof(buffer), "RSSI: %ld dBm", (long)st.rssi);
    SSD1306_Drawtext(0, 40, buffer, 1);
  }
  else
  {
    SSD1306_Drawtext(0, 40, "RSSI: ---", 1);
  }

  SSD1306_Drawtext(0, 50, st.internet_ok ? "NET: OK" : "NET: FAIL", 1);

  SSD1306_Display(&display);
  vTaskDelay(pdMS_TO_TICKS(500));
}

void normal_mode_display()
{
  char buffer[32];

  if (inWaveMode)
  {
    // Al salir del modo sismógrafo Limpiamos pantalla para evitar que quede la
    // onda debajo del texto
    SSD1306_ClearDisplay();
    inWaveMode = false;
  }

  SSD1306_ClearDisplay();

  // Hora
  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);
  strftime(buffer, sizeof(buffer), "CLOCK <%H:%M:%S>", &timeinfo);
  SSD1306_Drawtext(0, 0, buffer, 1);

  // IMU
  snprintf(buffer, sizeof(buffer), "A:%.2f %.2f %.2f", last_sample.accX,
           last_sample.accY, last_sample.accZ);
  SSD1306_Drawtext(0, 10, buffer, 1);

  snprintf(buffer, sizeof(buffer), "G:%.2f %.2f %.2f", last_sample.gyroX,
           last_sample.gyroY, last_sample.gyroZ);
  SSD1306_Drawtext(0, 20, buffer, 1);

  snprintf(buffer, sizeof(buffer), "I:%.3f %.3f", last_sample.inclX,
           last_sample.inclY);
  SSD1306_Drawtext(0, 30, buffer, 1);

  if (sample_status.active || sample_status.progress > 0.0f)
  {
    const int barWidth = SSD1306_LCDWIDTH - 10;
    const int barHeight = 6;
    const int barX = 5;
    const int barY = SSD1306_LCDHEIGHT - barHeight - 2;

    int filled = (int)(sample_status.progress * barWidth);
    if (filled > barWidth)
      filled = barWidth;

    SSD1306_DrawRect(barX, barY, barWidth, barHeight);
    if (filled > 0)
      SSD1306_FillRect(barX + 1, barY + 1, filled - 2, barHeight - 2, 1);
  }

  SSD1306_Display(&display);
  vTaskDelay(pdMS_TO_TICKS(200)); // refresco moderado
}

void icon_mode_display()
{
  SSD1306_ClearDisplay();

  // --- Columna izquierda: icono ---
  SSD1306_DrawBMP(&display, 0, 0, (uint8_t *)wolf_64x64, 64, 64);

  // --- Columna derecha: info ---
  int col_x = 46; // icono ancho + margen
  int y = 0;
  int line_height = 10; // ajusta según fuente

  char buffer[32];

  // Primera columna de texto
  snprintf(buffer, sizeof(buffer), "SW: %s", VER_SOFTWARE);
  SSD1306_Drawtext(col_x, y, buffer, 1);
  y += line_height;

  snprintf(buffer, sizeof(buffer), "HW: %s", VER_HARDWARE);
  SSD1306_Drawtext(col_x, y, buffer, 1);
  y += line_height;

  // snprintf(buffer, sizeof(buffer), "MF: %s", manufacturer);
  // SSD1306_Drawtext(col_x, y, buffer, 1);
  // y += line_height;

  // Primera línea del fabricante
  SSD1306_Drawtext(col_x, y, "MF: Lycans", 1);
  y += line_height;

  // Segunda línea del fabricante
  SSD1306_Drawtext(col_x + 15, y, "Electronics", 1);
  y += line_height;

  // Segunda columna (abajo de la primera)
  snprintf(buffer, sizeof(buffer), "YR: 20%02d", yearfactory);
  SSD1306_Drawtext(col_x, y, buffer, 1);
  y += line_height;

  // Segunda columna (abajo de la primera)
  // Usar datos de la variable global (actualizada por la tarea)
  snprintf(buffer, sizeof(buffer), "BT: %.2f/%d%%", battery.voltage, (int)roundf(battery.soc));

  SSD1306_Drawtext(col_x, y, buffer, 1);
  y += line_height;

  SSD1306_Display(&display);
  vTaskDelay(pdMS_TO_TICKS(200)); // refresco moderado
}

void tilt_mode_display()
{
  SSD1306_ClearDisplay();

  int cx = SSD1306_LCDWIDTH / 2;
  int cy = SSD1306_LCDHEIGHT / 2;
  int radius = (SSD1306_LCDHEIGHT / 2) - 2;
  SSD1306_DrawCircle(cx, cy, radius);

  // Calcular ángulo de inclinación total
  float tilt_deg = sqrtf(last_sample.inclX * last_sample.inclX +
                         last_sample.inclY * last_sample.inclY);

  // DIRECCIÓN según convención del datasheet:
  // atan2(Y, X) donde 0° = +X (derecha), 90° = +Y (arriba)
  float direction_angle = atan2f(last_sample.inclY, last_sample.inclX);

  // Escalar y limitar inclinación
  if (tilt_deg > 90.0f)
    tilt_deg = 90.0f;

  // Calcular posición de la aguja
  float scaled_radius = (radius - 2) * (tilt_deg / 90.0f);
  int x = cx + (int)(cosf(direction_angle) * scaled_radius);
  int y = cy - (int)(sinf(direction_angle) * scaled_radius);

  // Dibujar aguja
  SSD1306_DrawLine(cx, cy, x, y, 1);

  // Dibujar ejes de referencia
  SSD1306_DrawLine(cx - radius, cy, cx + radius, cy, 1); // Eje X
  SSD1306_DrawLine(cx, cy - radius, cx, cy + radius, 1); // Eje Y

  // Etiquetar ejes según datasheet
  SSD1306_Drawtext(cx + radius - 8, cy + 8, "+X", 1);  // Derecha
  SSD1306_Drawtext(cx - 12, cy - radius - 8, "+Y", 1); // Arriba
  SSD1306_Drawtext(cx - radius + 2, cy + 8, "-X", 1);  // Izquierda
  SSD1306_Drawtext(cx - 8, cy + radius - 8, "-Y", 1);  // Abajo

  // Mostrar valores
  char buffer[32];
  char dir_buffer[20];

  // Inclinación total
  snprintf(buffer, sizeof(buffer), "Tilt: %.1f°", tilt_deg);
  SSD1306_Drawtext(0, SSD1306_LCDHEIGHT - 30, buffer, 1);

  // Dirección (0° = +X/derecha, 90° = +Y/arriba)
  float direction_deg = direction_angle * 180.0f / M_PI;
  if (direction_deg < 0)
    direction_deg += 360.0f;

  // Determinar dirección según datasheet
  const char *direction_str = "Right";
  if (direction_deg >= 45 && direction_deg < 135)
    direction_str = "Up";
  else if (direction_deg >= 135 && direction_deg < 225)
    direction_str = "Left";
  else if (direction_deg >= 225 && direction_deg < 315)
    direction_str = "Down";
  else
    direction_str = "Right";

  snprintf(dir_buffer, sizeof(dir_buffer), "Dir: %.0f° %s", direction_deg,
           direction_str);
  SSD1306_Drawtext(0, SSD1306_LCDHEIGHT - 20, dir_buffer, 1);

  // Componentes individuales (valores en g)
  snprintf(buffer, sizeof(buffer), "X:%+.2fg Y:%+.2fg", last_sample.inclX,
           last_sample.inclY);
  SSD1306_Drawtext(0, SSD1306_LCDHEIGHT - 10, buffer, 1);

  SSD1306_Display(&display);
  vTaskDelay(pdMS_TO_TICKS(200));
}

void system_metrics_mode_display(void)
{

  // Uptime: 00d 02h 15m 32s
  // CPU: 11% uso
  // RAM: 82/128 KB
  // PSRAM: 7800/8192 KB

  SSD1306_ClearDisplay();

  char buffer[128];
  get_system_metrics_summary(buffer, sizeof(buffer));

  // Garantiza que siempre termine en '\0', incluso si snprintf truncó
  buffer[sizeof(buffer) - 1] = '\0';

  // --- Dibujar cada línea ---
  char line[32];
  const char *ptr = buffer;
  int y = 0;

  while (*ptr && y < (SSD1306_LCDHEIGHT - 10))
  {
    int len = 0;
    // Busca fin de línea o fin de cadena
    while (ptr[len] != '\n' && ptr[len] != '\0' && len < (sizeof(line) - 1))
      len++;

    // Copia segura de la línea
    strncpy(line, ptr, len);
    line[len] = '\0'; // cierre garantizado

    // Dibuja la línea
    SSD1306_Drawtext(0, y, line, 1);
    y += 10;

    // Avanza al siguiente carácter (si hay '\n')
    ptr += (ptr[len] == '\n') ? len + 1 : len;
  }

  SSD1306_Display(&display);
  vTaskDelay(pdMS_TO_TICKS(1000)); // refresco cada 1 segundo
}

void display_task(void *pvParameters)
{
  while (1)
  {
    // === MODO PÁGINAS ===
    switch (currentPage)
    {
      // === MODO NORMAL ===
    case 0:
      normal_mode_display();
      break;
      // === MODO INFO RED ===
    case 1:
      info_NET_mode_display();
      break;
    case 2:
      // === MODO SISMÓGRAFO ===
      sismic_mode_display();
      break;
      // === MODO INCLINACIÓN ===
    case 3:
      tilt_mode_display();
      break;
      // === MODO MÉTRICAS DEL SISTEMA ===
    case 4:
      system_metrics_mode_display();
      break;
      // === MODO ABOUT ===
    case 5:
      icon_mode_display();
      break;

    default:
      currentPage = 0;
      break;
    }
  }
}
