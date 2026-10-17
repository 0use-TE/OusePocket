from pathlib import Path
import sys
import serial
import bpk

original_read = serial.Serial.read
with Path("serial-install.log").open("wb") as transcript:
    def capture_read(port, size=1):
        data = original_read(port, size)
        transcript.write(data)
        transcript.flush()
        return data

    serial.Serial.read = capture_read
    sys.argv = ["bpk.py", *sys.argv[1:]]
    bpk.main()
