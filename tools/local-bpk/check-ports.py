import sys
import serial
from serial.tools import list_ports

print("Python:", sys.version)
print("pyserial:", serial.VERSION)
print("Detected ports:")
for port in list_ports.comports():
    print(port.device, port.description)
