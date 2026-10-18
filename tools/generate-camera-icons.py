"""Build original rounded camera/gallery icons from code-native shapes."""
from pathlib import Path
import struct
from PIL import Image, ImageDraw

ROOT=Path(__file__).resolve().parents[1]
for name in ('camera','gallery'):
    image=Image.new('RGBA',(256,256),(0,0,0,0))
    draw=ImageDraw.Draw(image)
    draw.rounded_rectangle((4,4,252,252),radius=60,fill='#26344A' if name=='camera' else '#7260CD')
    if name=='camera':
        draw.rounded_rectangle((82,51,177,100),radius=15,fill='#E0E8F5')
        draw.rounded_rectangle((40,79,217,202),radius=24,fill='#E0E8F5')
        draw.ellipse((83,95,175,187),fill='#26344A')
        draw.ellipse((101,113,157,169),fill='#58BBD0')
        draw.ellipse((110,119,124,133),fill='#FFFFFF')
        draw.rounded_rectangle((184,96,201,108),radius=5,fill='#F7BE51')
    else:
        draw.rounded_rectangle((51,41,210,197),radius=15,fill='#B6A8ED')
        draw.rounded_rectangle((35,62,194,218),radius=15,fill='#FFFFFF')
        draw.rounded_rectangle((49,77,180,203),radius=8,fill='#D9F0F5')
        draw.ellipse((133,91,164,122),fill='#F6BD54')
        draw.polygon(((49,183),(92,122),(126,165),(146,140),(180,184),(180,203),(49,203)),fill='#5EAAA0')
    image=image.resize((64,64),Image.Resampling.LANCZOS)
    (ROOT/'apps'/name/'icon.bin').write_bytes(b'OIC1'+struct.pack('<HH',64,64)+image.tobytes('raw','BGRA'))
