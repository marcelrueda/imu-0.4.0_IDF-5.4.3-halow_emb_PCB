# Directorio: net

## Autoría
- **Nombre:** Carlos Andres Vargas Calderon
- **Correo:** carlos_vargas_c@outlook.com
- **Profesión:** Ing Electronico
- **Empresa:** Lycans Electronics - 2026

## Descripción de Archivos
Pila de protocolos de comunicación y conectividad inalámbrica.

- **network_config.h:** Configuraciones generales de red, credenciales y parámetros de conexión.
- **network_selector.c / .h:** Lógica para seleccionar el medio de transporte activo (WiFi, HaLow, etc.).
- **network_wifi24G.c / .h:** Gestión de la conexión WiFi en la banda de 2.4GHz.
- **network_halow_mm.c / .h:** Integración con la tecnología Wi-Fi HaLow (Morse Micro).
- **network_mqtt.c / .h:** Implementación del protocolo MQTT para telemetría.
- **network_http_client.c / .h:** Cliente HTTP para peticiones REST o descarga de datos.
- **network_tcp_client.c / .h:** Cliente TCP para comunicaciones directas por socket.
- **server_websocket.c / .h:** Servidor WebSocket para comunicación en tiempo real.
- **halow/:** Subdirectorio con archivos específicos de la implementación HaLow de Morse Micro.
