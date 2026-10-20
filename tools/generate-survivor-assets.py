"""Original, deterministic pixel art for the survivor game. Requires Pillow."""
from pathlib import Path
import struct
from PIL import Image,ImageDraw
root=Path(__file__).resolve().parents[1]/'apps/survivor'
root.joinpath('assets').mkdir(parents=True,exist_ok=True)
def tile(frame):
 if frame==50:return extra_tile(52)
 if frame==51:return extra_tile(55)
 return extra_tile(frame)
def extra_tile(frame):
 if frame>=48:
  im=Image.new('RGBA',(24,24));d=ImageDraw.Draw(im)
  if frame==48:
   d.polygon([(12,2),(16,9),(15,18),(9,18),(8,9)],fill='#d5dfc3',outline='#425755');d.rectangle((10,8,14,13),fill='#cb6948');d.polygon([(9,18),(15,18),(12,23)],fill='#ffc666')
  elif frame in (49,50):
   d.polygon([(12,1),(15,8),(23,8),(17,14),(20,22),(12,17),(4,22),(7,14),(1,8),(9,8)],fill='#ffbf64' if frame==49 else '#df7349');d.ellipse((8,8,16,16),fill='#fff2b4')
  elif frame==51:
   d.polygon([(12,4),(19,12),(12,20),(5,12)],fill='#72ced2');d.line((12,7,12,17),fill='#e1ffff',width=2);d.line((7,12,17,12),fill='#e1ffff',width=2)
  elif frame in (52,53):
   d.polygon([(11,1),(15,5),(14,16),(10,16)],fill='#d7ece8',outline='#718c9c');d.rectangle((6,16,18,18),fill='#e4c478');d.rectangle((11,18,13,23),fill='#825c4a')
   if frame==53:im=im.rotate(-45,resample=Image.Resampling.NEAREST)
  else:
   base=tile(4 if frame==55 else 6)
   for y in range(24):
    for x in range(24):
     r,g,b,a=base.getpixel((x,y))
     if a and r>90:base.putpixel((x,y),(min(255,r//2+35),min(255,g+65),min(255,b+35),a) if frame==55 else (min(255,r+15),min(255,g+30),min(255,b+65),a))
   im=base
  return im
 if frame==14:
  im=tile(6);d=ImageDraw.Draw(im);d.rectangle((4,2,20,5),fill='#e2bd66');d.rectangle((6,10,9,13),fill='#fff0b0');d.rectangle((15,10,18,13),fill='#fff0b0');return im
 if frame>=16:
  return tile(11).rotate(-(frame-16)*360/32,resample=Image.Resampling.NEAREST,center=(12,12))
 if frame>=13:
  return tile(11).transpose({13:Image.Transpose.FLIP_LEFT_RIGHT,14:Image.Transpose.ROTATE_90,15:Image.Transpose.ROTATE_270}[frame])
 im=Image.new('RGBA',(24,24));d=ImageDraw.Draw(im)
 if frame<4:
  bob=frame%2;d.ellipse((3,19,21,23),fill='#152c2d')
  d.rectangle((6,17+bob,10,21+bob),fill='#c48b4c');d.rectangle((14,17-bob,18,21-bob),fill='#c48b4c')
  d.rounded_rectangle((4,3+bob,20,19+bob),radius=6,fill='#ddae6a',outline='#422e32',width=2)
  d.rectangle((5,5+bob,19,9+bob),fill='#739998');d.rectangle((7,4+bob,17,6+bob),fill='#a8c5ac')
  d.rectangle((5,10+bob,19,14+bob),fill='#283d4c');d.rectangle((7,11+bob,10,12+bob),fill='#d1faf1');d.rectangle((14,11+bob,17,12+bob),fill='#d1faf1')
  d.rectangle((10,16+bob,14,17+bob),fill='#734a38')
 elif frame<8:
  brute=frame>=6;bob=frame%2;c='#b86f9a' if brute else '#c25f51';light='#ef9ab1' if brute else '#f19b68'
  d.ellipse((2,18,22,23),fill='#172c2d');d.polygon([(3,7+bob),(7,3),(17,3),(21,7+bob),(21,18),(16,21),(7,21),(3,17)],fill=c)
  d.rectangle((5,6+bob,18,9+bob),fill=light);d.rectangle((6,11+bob,9,14+bob),fill='#241c31');d.rectangle((15,11+bob,18,14+bob),fill='#241c31')
  d.rectangle((9,17,15,18),fill='#f9dfbb');d.line((3,7,0,2),fill=c,width=2);d.line((21,7,23,2),fill=c,width=2)
 elif frame==8:
  d.ellipse((8,8,16,16),fill='#d89436');d.rectangle((10,10,14,14),fill='#fff2a8')
 elif frame==9:
  d.polygon([(12,6),(17,12),(12,18),(7,12)],fill='#51b7a0');d.polygon([(12,7),(14,11),(12,14),(10,11)],fill='#c1f3ac')
 elif frame==10:
  d.polygon([(3,16),(5,9),(11,6),(19,9),(22,17),(17,21),(6,21)],fill='#3e5750');d.line((6,12,11,9,17,11),fill='#6a7c60',width=2)
 elif frame==11:
  d.rectangle((4,9,18,14),fill='#20313b');d.rectangle((8,7,18,12),fill='#a4adb0');d.rectangle((18,9,23,11),fill='#d9d4af');d.rectangle((8,14,12,19),fill='#7e5747')
 elif frame==12:
  d.rectangle((6,6,18,18),fill='#da6654');d.rectangle((10,3,14,21),fill='#f2d6a6');d.rectangle((3,10,21,14),fill='#f2d6a6')
 return im
atlas=Image.new('RGBA',(24*13,24*4))
for i in range(52):atlas.paste(tile(i),(24*(i%13),24*(i//13)))
def save(im,path):
 path.write_bytes(b'OIC1'+struct.pack('<HH',*im.size)+im.tobytes('raw','BGRA'))
save(atlas,root/'assets/actors.bin')
save(tile(0).resize((52,52),Image.Resampling.NEAREST),root/'assets/badge.bin')
icon=Image.new('RGBA',(80,80),'#263e3c');d=ImageDraw.Draw(icon);d.rounded_rectangle((3,3,76,76),radius=18,outline='#6c9474',width=3)
icon.alpha_composite(tile(0).resize((60,60),Image.Resampling.NEAREST),(10,7));icon.alpha_composite(tile(11).resize((32,32),Image.Resampling.NEAREST),(44,43));save(icon,root/'icon.bin');icon.save(root/'icon.png')
print('Generated original pixel atlas and icon')
