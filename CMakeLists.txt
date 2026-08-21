# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

set(MMIOT_ROOT ${CMAKE_SOURCE_DIR}/components/mm-iot-esp32)  #  Local en  components
set(ENV{MMIOT_ROOT} ${MMIOT_ROOT})                           # Variable de entorno
message(STATUS "MMIOT_ROOT: ${MMIOT_ROOT}")

# Workaround to allow us to use the link status callback in LWIP. The ESP-IDF does not current (as
# of v5.1.1) exposed this option from the lwip (esp-lwip) component.
add_compile_definitions(LWIP_NETIF_LINK_CALLBACK=1)

if(DEFINED COUNTRY_CODE)
    message (Using country code: ${COUNTRY_CODE})
    add_compile_definitions(COUNTRY_CODE="${COUNTRY_CODE}")
endif()

set(EXTRA_COMPONENT_DIRS $ENV{IDF_PATH}/examples/common_components/protocol_examples_common components)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(imu)
