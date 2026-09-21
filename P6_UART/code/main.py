import serial
import time

PORT = "/dev/tty.usbmodem14401"

ser = serial.Serial(
    PORT,
    115200,
    timeout=0,
    write_timeout=1
)

texto = (
    b"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    b"0123456789"
    b"abcdefghijklmnopqrstuvwxyz\r\n"
)

enviados = 0
fin = time.monotonic() + 30

print("Iniciando prueba de 30 segundos...")

try:
    while time.monotonic() < fin:

        enviados += ser.write(texto)

        # Leer y descartar eco/log que regresa
        if ser.in_waiting:
            ser.read(ser.in_waiting)

finally:
    ser.close()

print("Prueba terminada")
print("Bytes enviados:", enviados)