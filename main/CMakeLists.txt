set(srcs
    "src/main.c"
    # BUS
    "src/bus/bus_i2c.c"
    "src/bus/bus_spi.c"
    # COMMON
    "src/common/utils.c"
    # IMAGE
    "src/image/imagen.c"
     # IMU
    "src/imu/data_sensor.c"
    "src/imu/device_icm42670p.c"
    "src/imu/device_mpu6500.c"
    "src/imu/device_SCL3400.c"
    "src/imu/imu_tools.c"
    "src/imu/imu.c"
    # NETWORK
    "src/net/network_halow_mm.c"
    "src/net/network_mqtt.c"
    "src/net/network_selector.c"
    "src/net/network_tcp_client.c"
    "src/net/network_wifi24G.c"
    "src/net/server_websocket.c"
    # HALOW (ahora parte de net)
        "src/net/halow/mm_app_common.c"
        "src/net/halow/mm_app_loadconfig.c"
        "src/net/halow/mm_app_regdb.c"
    # PERIPHERALS
    "src/peripherals/device_display.c"
    "src/peripherals/device_sd.c"
    "src/peripherals/sensor_analog.c"
    "src/peripherals/max17043.c"
    # SYSTEM
    "src/sys/system_metrics.c"
    "src/sys/time_ntp.c"
    "src/sys/time_utils.c"
)

idf_component_register(
    SRCS ${srcs}

    INCLUDE_DIRS
        "src"
        "src/bus"
        "src/common"
        "src/image"
        "src/imu" 
        "src/net"
        "src/net/halow"
        "src/peripherals"
        "src/sys"

    REQUIRES
        esp_netif esp_wifi
        nvs_flash
        lwip
        mqtt
        json
        mbedtls
        zlib
        esp_http_client
        esp_http_server
        spiffs
        led_strip
        mmutils morselib mm_shims mmipal

    PRIV_REQUIRES
        driver
        esp_timer
        esp_adc
        fatfs
        sdmmc
        helper_i2c
        ssd1306
)

# =============================
# Configuración de red
# =============================

 set(NETWORK_MODE "WIFI2G")
#set(NETWORK_MODE "HALOW")

if(NETWORK_MODE STREQUAL "WIFI2G")
    target_compile_definitions(${COMPONENT_LIB} PUBLIC NETWORK_MODE_WIFI2G=1)
elseif(NETWORK_MODE STREQUAL "HALOW")
    target_compile_definitions(${COMPONENT_LIB} PUBLIC NETWORK_MODE_HALOW=1)
endif()

target_compile_definitions(${COMPONENT_LIB} PUBLIC
    NETWORK_MODE=STATIC
    STATIC_LOCAL_IP="192.168.1.3"
    STATIC_GATEWAY="192.168.1.1"
    STATIC_NETMASK="255.255.255.0"
)
#crear particion para spiffs
spiffs_create_partition_image(storage ../data FLASH_IN_PROJECT)
