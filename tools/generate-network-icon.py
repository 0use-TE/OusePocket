from pathlib import Path
import struct
from PIL import Image, ImageDraw

root = Path(__file__).resolve().parents[1]
image = Image.new('RGBA', (256, 256), (0, 0, 0, 0))
draw = ImageDraw.Draw(image)
draw.rounded_rectangle((4, 4, 252, 252), radius=60, fill='#197BBD')
draw.line((71, 91, 128, 165, 185, 91), fill='white', width=14)
draw.line((71, 91, 185, 91), fill='white', width=14)
for horizontal, vertical in [(71, 91), (185, 91), (128, 165)]:
    draw.ellipse((horizontal - 23, vertical - 23, horizontal + 23, vertical + 23), fill='white')
    draw.ellipse((horizontal - 10, vertical - 10, horizontal + 10, vertical + 10), fill='#197BBD')
image = image.resize((64, 64), Image.Resampling.LANCZOS)
destination = root / 'apps/network/icon.bin'
destination.write_bytes(b'OIC1' + struct.pack('<HH', 64, 64) + image.tobytes('raw', 'BGRA'))
mode = destination.parent / 'assets/mode.bin'
mode.parent.mkdir(exist_ok=True)
mode.write_bytes((root / 'apps/calculator/assets/mode.bin').read_bytes())
