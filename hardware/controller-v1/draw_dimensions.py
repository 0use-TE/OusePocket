from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
import json,math
OUT=Path(__file__).resolve().parent
p=json.loads((OUT/'parameters.json').read_text());layout=json.loads((OUT/'layout.json').read_text())
im=Image.new('RGB',(2200,1600),'white');d=ImageDraw.Draw(im)
fontpath='C:/Windows/Fonts/msyh.ttc'
def f(n):return ImageFont.truetype(fontpath,n)
def text(x,y,s,n=26,color='#243040'):d.text((int(x),int(y)),s,font=f(n),fill=color)
def line(points,color='#677485',width=2):d.line(points,fill=color,width=width)
def arrow(a,b):
 line([a,b]);dx=b[0]-a[0];dy=b[1]-a[1];l=math.hypot(dx,dy);ux,uy=dx/l,dy/l
 for point,sgn in [(a,1),(b,-1)]:
  x,y=point;d.polygon([(x,y),(x+sgn*ux*12-uy*5,y+sgn*uy*12+ux*5),(x+sgn*ux*12+uy*5,y+sgn*uy*12-ux*5)],fill='#677485')
def centered(x,y,s,n=24):
 b=d.textbbox((0,0),s,font=f(n));text(x-(b[2]-b[0])/2,y,s,n)
text(70,42,'OusePocket 控制器 V1 · 机械尺寸',44)
text(74,104,'单位 mm｜主机横屏，上屏下键｜ABXY＋摇杆按下＋十字键＋Menu＋Start',26)
S=6;cx,cy=430,740
xy=lambda x,y:(cx+x*S,cy-y*S)
def rect(x,y,w,h,fill,r=0,outline='#25313e'):
 a=xy(x-w/2,y+h/2);b=xy(x+w/2,y-h/2)
 d.rounded_rectangle([a,b],radius=r*S,fill=fill,outline=outline,width=2)
def circle(x,y,r,fill):
 a=xy(x-r,y+r);b=xy(x+r,y-r);d.ellipse([a,b],fill=fill,outline='#25313e',width=2)
rect(0,0,110,168,'#dee2e8',9);rect(0,46,95.11,63.67,'#252c35',7.3);rect(0,46,73.44,48.96,'#254857',0)
text(cx-80,cy-46*S,'480 × 320',27,'#e9eff6')
circle(-28,-8,10,'#303846');px,py=xy(-28,-8);text(px-13,py-13,'L3',21,'#e9eff6')
colors={'A':'#d96f75','B':'#e2b956','X':'#6b9ed9','Y':'#6eb48c'}
for n in ['A','B','X','Y']:
 x,y=layout['buttons'][n];circle(x,y,5,colors[n]);px,py=xy(x,y);centered(px,py-14,n,22)
x,y=-28,-47;coords=[(-4,-13.5),(4,-13.5),(4,-4),(13.5,-4),(13.5,4),(4,4),(4,13.5),(-4,13.5),(-4,4),(-13.5,4),(-13.5,-4),(-4,-4)]
d.polygon([xy(x+a,y+b) for a,b in coords],fill='#303846',outline='#243040')
for n in ['Menu','Start']:
 x,y=layout['buttons'][n];circle(x,y,4,'#303846');px,py=xy(x,y);centered(px,py+31,n.upper(),19)
x1,y1=xy(-55,84);x2,y2=xy(55,84)
line([(x1,y1),(x1,y1-65)]);line([(x2,y2),(x2,y2-65)]);arrow((x1,y1-53),(x2,y2-53));centered(cx,y1-90,'110',28)
a=xy(55,84);b=xy(55,-84);xx=b[0]+70
line([a,(xx+15,a[1])]);line([b,(xx+15,b[1])]);arrow((xx,a[1]),(xx,b[1]));text(xx+13,(a[1]+b[1])/2-18,'168',28)
text(100,1305,'外壳本体：110 × 168 × 28',30)
text(100,1354,'主机：95.11 × 63.67 × 14.10（整机占位）',25)
text(100,1395,'ABXY：帽 Φ10 / 孔 Φ10.5 / 相邻对角中心距 17.68',24)
text(100,1436,'摇杆帽：Φ20，顶面高出面板 9.5；十字键：27 × 27',24)
# Two section diagrams, same true Z scale, with simplified envelopes.
text(1100,190,'高度剖面：普通按钮',31);text(1640,190,'高度剖面：摇杆',31)
zpix=lambda z:400-z*14
for col in [1220,1760]:
 d.rectangle([col-125,zpix(0),col+125,zpix(-2.4)],fill='#d5dbe3',outline='#596879')
 d.rectangle([col-125,zpix(-25.6),col+125,zpix(-28)],fill='#566679',outline='#243040')
# Main button / panel aperture
col=1220
d.rectangle([col-74,zpix(1.6),col+74,zpix(-2.4)],fill='#d96f75',outline='#25313e')
d.rectangle([col-87,zpix(-2.4),col+87,zpix(-3.2)],fill='#d96f75',outline='#25313e')
d.rectangle([col-20,zpix(-3.2),col+20,zpix(-3.55)],fill='#d96f75',outline='#25313e')
d.rectangle([col-25,zpix(-3.7),col+25,zpix(-4.5)],fill='#313b49')
d.rectangle([col-42,zpix(-4.5),col+42,zpix(-8)],fill='#313b49')
d.rectangle([col-125,zpix(-8),col+125,zpix(-9.6)],fill='#3e9477',outline='#25313e')
for xx in [col-100,col+100]:
 d.rectangle([xx-18,zpix(-9.6),xx+18,zpix(-10.6)],fill='#d9d9c9',outline='#6c6d62')
 d.rectangle([xx-16,zpix(-10.6),xx+16,zpix(-25.6)],fill='#bf9954',outline='#705424')
# Joy carrier / body / shaft / cap
col=1760
d.rectangle([col-125,zpix(-18.8),col+125,zpix(-20.4)],fill='#3e9477',outline='#25313e')
d.rectangle([col-125,zpix(-7.6),col+125,zpix(-18.8)],fill='#727d87',outline='#25313e')
d.rectangle([col-28,zpix(.15),col+28,zpix(-7.6)],fill='#2d3642')
d.rectangle([col-45,zpix(6.5),col+45,zpix(-4.2)],fill='#303846',outline='#25313e')
d.rectangle([col-140,zpix(9.5),col+140,zpix(6.5)],fill='#303846',outline='#25313e')
for xx in [col-105,col+105]:d.rectangle([xx-16,zpix(-20.4),xx+16,zpix(-25.6)],fill='#566679',outline='#25313e')
for z,label in [(0,'Z = 0'),(-8,'Z = -8'),(-18.8,'Z = -18.8'),(-28,'Z = -28')]:
 yy=zpix(z);line([(1070,yy),(1090,yy)]);text(970,yy-13,label,19)
text(1090,875,'普通按钮高度链',29)
text(1090,930,'PCB顶面 -8.00 → 开关顶面 -3.70',24)
text(1090,975,'按钮顶柱底面 -3.55 → 松开间隙 0.15',24)
text(1090,1020,'按压总量 0.40 = 间隙 0.15＋开关行程 0.25',24)
text(1090,1065,'支撑：15 mm 铜柱＋名义 1.0 mm 调整垫片',24)
text(1090,1110,'垫片用于补偿开关高度及打印误差，需试装调整。',22)
text(1090,1195,'摇杆：RKJXV122400R',29)
text(1090,1245,'模块 18.2×21.7×11.2；轴顶距安装基准 18.95',24)
text(1090,1290,'±23° 全方向倾斜；按下名义 0.4，包络按 0.9 检查',24)
text(1090,1335,'单独 32×32×1.6 小板；帽孔双平面 4.1×3.1 试配',24)
text(1090,1415,'主机排母、相机及原壳细节沿用估算模型，尚未实物核对。',22,'#9e5937')
text(1090,1455,'当前是机械试装设计；PCB无布线，元件内部形状简化。',22,'#9e5937')
im.save(OUT/'dimensions.png')
