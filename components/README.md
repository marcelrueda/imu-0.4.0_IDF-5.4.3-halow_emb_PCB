Components


https://github.com/UncleRus/esp-idf-lib


Paso 1: Crear la carpeta components
/tu_proyecto/
│── CMakeLists.txt
│── sdkconfig
│── main/
│   ├── CMakeLists.txt
│   ├── app_main.c
│── components/   <-- Aquí irán los componentes personalizados
│── build/
│── ...


Paso 2: Descargar el código del componente
Si el driver PCF8574 está en GitHub u otra fuente, clónalo en components:
cd components
git clone https://github.com/usuario/pcf8574.git
Esto creará la carpeta:
/tu_proyecto/components/pcf8574/
│── CMakeLists.txt
│── include/
│── src/
│── ...


Paso 3: Configurar CMake
idf_component_register(SRCS "src/pcf8574.c"
                       INCLUDE_DIRS "include"
                       REQUIRES driver)


Paso 4: Editar CMakeLists.txt en el directorio raíz
set(EXTRA_COMPONENT_DIRS components)


 Paso 5: Incluir la librería en tu código
 #include "pcf8574.h"




