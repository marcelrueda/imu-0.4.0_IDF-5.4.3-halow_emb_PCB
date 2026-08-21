## Generador de datos simulados para el sensor MPU6500
# Este script genera un paquete de datos simulados para el sensor MPU6500,

import json
import random
from datetime import datetime

def generate_sample():
    # Valores simulados dentro de rangos realistas
    return [
        round(random.uniform(-0.2, 0.2), 2),  # AccX (g)
        round(random.uniform(-0.2, 0.2), 2),  # AccY (g)
        round(random.uniform(0.95, 1.05), 2), # AccZ (g, gravitacional)
        round(random.uniform(-0.1, 0.1), 2),  # Gx (deg/s)
        round(random.uniform(-0.1, 0.1), 2),  # Gy (deg/s)
        round(random.uniform(0.4, 0.6), 2),   # Gz (deg/s)
        round(random.uniform(0.0, 0.02), 2),  # Incx (units)
        round(random.uniform(0.0, 0.02), 2)   # Incy (units)
    ]

def generate_packet():
    packet = {
        "timestamp": "2025-06-08T14:23:00Z",
        "sensor_id": "mpu6500_azotea",
        "sampling_rate": 30,
        "duration": 60,
        "units": {
            "acceleration": "g",
            "gyroscope": "deg/s",
            "incremental": "units"
        },
        "data": [generate_sample() for _ in range(1800)]
    }
    return packet

def save_packet_to_file(packet, filename="data_packet.json"):
    with open(filename, "w") as f:
        json.dump(packet, f, indent=2)
    print(f"Archivo '{filename}' creado con éxito.")

# Generar paquete
packet = generate_packet()

# Guardar en archivo JSON
save_packet_to_file(packet)

# Mostrar extracto parcial en consola
print("\nJSON parcial generado:\n")
print(json.dumps({
    **{k: v for k, v in packet.items() if k != "data"},
    "data": packet["data"][:3] + ["..."] + packet["data"][-3:]
}, indent=2))
