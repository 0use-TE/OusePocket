"""Generate the store's OIC1 icon from vector shapes."""
from pathlib import Path
import struct
from PIL import Image, ImageDraw

root = Path(__file__).resolve().parents[1]
image = Image.new('RGBA', (256, 256), '#E5EEFF')
draw = ImageDraw.Draw(image)
draw.rounded_rectangle((46, 76, 210, 218), radius=30, fill='#2865DF')
draw.arc((88, 35, 168, 121), 180, 360, fill='#2865DF', width=16)
draw.line((128, 110, 128, 173), fill='white', width=13)
draw.line((100, 150, 128, 178, 156, 150), fill='white', width=13, joint='curve')
image = image.resize((64, 64), Image.Resampling.LANCZOS)
(root / 'apps/store/icon.bin').write_bytes(b'OIC1' + struct.pack('<HH', 64, 64) + image.tobytes('raw', 'BGRA'))
