"""Build bounded OIC1 slots for the P4 store's downloaded icons."""
from pathlib import Path
import struct
from PIL import Image,ImageDraw
root=Path(__file__).resolve().parents[1]/'apps/store/assets'
root.mkdir(parents=True,exist_ok=True)
image=Image.new('RGBA',(48,48),'#E5EEFF')
draw=ImageDraw.Draw(image)
draw.rounded_rectangle((10,10,38,38),radius=7,fill='#8AA9DC')
draw.line((24,16,24,32),fill='white',width=3)
draw.line((18,26,24,32,30,26),fill='white',width=3)
data=b'OIC1'+struct.pack('<HH',48,48)+image.tobytes('raw','BGRA')
for name in ['placeholder']+['slot'+str(index) for index in range(12)]:
    (root/(name+'.bin')).write_bytes(data)
