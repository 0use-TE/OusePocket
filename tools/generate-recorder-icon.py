from pathlib import Path
import struct
from PIL import Image, ImageDraw

image = Image.new('RGBA', (256, 256), (0, 0, 0, 0))
draw = ImageDraw.Draw(image)
draw.rounded_rectangle((4, 4, 252, 252), radius=60, fill='#DB4358')
draw.rounded_rectangle((100, 43, 156, 151), radius=28, fill='#FFFFFF')
draw.arc((77, 87, 179, 179), 0, 180, fill='#FFFFFF', width=12)
draw.rounded_rectangle((122, 167, 134, 203), radius=6, fill='#FFFFFF')
draw.rounded_rectangle((100, 198, 156, 210), radius=6, fill='#FFFFFF')
image = image.resize((64, 64), Image.Resampling.LANCZOS)
destination = Path(__file__).resolve().parents[1] / 'apps/recorder/icon.bin'
destination.write_bytes(b'OIC1' + struct.pack('<HH', 64, 64) + image.tobytes('raw', 'BGRA'))
