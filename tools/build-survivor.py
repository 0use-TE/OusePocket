"""Build original Survivor sprites, local audio and the shared-engine BPK."""
from pathlib import Path
import runpy,json,struct,math
from PIL import Image,ImageDraw
from bpk import pack
ROOT=Path(__file__).resolve().parents[1];APP=ROOT/'apps/survivor'

def page():
 def label(id,text='',size=18):return dict(type='label',id=id,x=0,y=0,width=100,height=30,text=text,fontSize=size,style={'textColor':'#efebcd'})
 def button(id,text):return dict(type='button',id=id,x=0,y=0,width=120,height=38,text=text,fontSize=18,style={'backgroundColor':'#d6cf9b','textColor':'#264737','radius':12})
 def group(id,children,hidden=True):return dict(type='container',id=id,x=0,y=0,width=320,height=240,style={'hidden':hidden},children=children)
 root=[dict(type='canvas',id='scene',width='fill',height='fill',fontSize=16),dict(type='canvas',id='hud',width='fill',height='fill',fontSize=16)]
 root.extend(dict(type='container',id=id,x=12,y=100,width=64,height=64,style={'hidden':True})for id in ['joy','skill','pause'])
 title=label('menuTitle','荒野生存',32);title.update(width=280,height=44)
 root.append(group('home',[title,button('menuStart','开始生存'),button('menuCamp','整备营地'),button('menuExit','返回桌面'),button('settings','设置')],False))
 children=[button('tab'+str(i),name) for i,name in enumerate(['角色','枪械','伙伴','区域'])]
 for i in range(6):children.append(dict(type='container',id='equip'+str(i),x=0,y=0,width=150,height=48,children=[label('equipName'+str(i),'',16)]))
 children += [label('previewName'),label('previewInfo','',16),button('campStart','出发'),button('campBack','返回')]
 root.append(group('camp',children))
 modal=group('modal',[label('title','',24),label('info','',16)]+[button('choice'+str(i),'')for i in range(3)])
 modal['style'].update(backgroundColor='#203e3a',radius=18,borderColor='#6c8971',borderWidth=1)
 root.append(modal)
 (APP/'page.json').write_text(json.dumps({'api':1,'units':'dp','root':{'type':'container','id':'page','width':'fill','height':'fill','children':root}},ensure_ascii=False,separators=(',',':'))+'\n',encoding='utf-8')

def assets():
 ns=runpy.run_path(str(ROOT/'tools/generate-survivor-assets.py'))
 atlas=Image.new('RGBA',(192,120))
 for index in range(40):
  im=Image.new('RGBA',(24,24));d=ImageDraw.Draw(im)
  if index<8:
   role=index//2;bob=index%2;color=['#6a945d','#6b8caa','#cc9455','#9b75b5'][role]
   d.ellipse((3,19,21,23),fill='#1e3034');d.rectangle((6,17+bob,10,22),fill='#434353');d.rectangle((14,17,18,22-bob),fill='#434353')
   d.rounded_rectangle((4,3+bob,20,19+bob),4,fill=color,outline='#263c45',width=2)
   d.rectangle((7,6+bob,17,10+bob),fill='#edc992');d.rectangle((6,10+bob,18,13+bob),fill='#294853');d.rectangle((8,11+bob,11,12+bob),fill='#b4e7db')
   if role==1:d.rectangle((1,10,5,17),fill='#a6bec8');d.rectangle((19,10,23,17),fill='#a6bec8')
   if role==2:d.rectangle((2,5,6,18),fill='#856347');d.rectangle((5,8,19,10),fill='#f0d664')
   if role==3:d.polygon([(4,10),(4,4),(12,0),(20,4),(20,10),(16,7),(8,7)],fill='#705180');d.line((16,16,23,20),fill='#dda1b8',width=3)
  elif index<16:
   pet=(index-8)//2;bob=index%2
   if pet==2:
    d.line((2,5,22,5),fill='#c3e1df',width=2);d.ellipse((6,6+bob,18,18+bob),fill='#c8dfba',outline='#3d6e65',width=2);d.rectangle((10,8+bob,14,15+bob),fill='#61ad8e');d.rectangle((8,10+bob,16,13+bob),fill='#61ad8e')
   else:
    col=['#bc8c58','#b2c1bf','#f2a25e'][pet if pet<2 else 2]
    d.ellipse((4,10+bob,20,21),fill=col,outline='#3b4046');d.polygon([(5,12),(3,2 if pet else 7),(10,8),(16,8),(21,2 if pet else 7),(20,14)],fill=col,outline='#3b4046')
    d.rectangle((7,11+bob,9,13+bob),fill='#1c3940');d.rectangle((15,11+bob,17,13+bob),fill='#1c3940');d.rectangle((10,16,14,17),fill='#f6ddba')
    d.line((18,18,23,10-bob*3),fill=col,width=3)
    if pet==3:d.polygon([(19,19),(22,9),(23,16)],fill='#f9d678')
  elif index<22:
   gun=index-16;cols=['#90ae91','#b08965','#d0b56e','#95b8c2','#8b9e8c','#74cfd8'];col=cols[gun]
   d.rectangle((3,10,20,14),fill='#263e48');d.rectangle((6,8,18,12),fill=col);d.rectangle((9,14,12,20),fill='#725448')
   if gun==1:d.rectangle((12,9,23,10),fill='#b9b8ac');d.rectangle((12,13,23,14),fill='#b9b8ac')
   if gun==2:d.rectangle((3,7,12,13),fill=col);d.rectangle((12,13,15,20),fill='#465b59')
   if gun==3:d.rectangle((10,5,18,7),fill='#47717e');d.rectangle((18,10,23,11),fill='#b8d4d2')
   if gun==4:d.ellipse((12,7,22,17),fill=col,outline='#2c4650');d.rectangle((16,10,22,14),fill='#334e53')
   if gun==5:d.line((13,4,10,10,17,9,14,19),fill='#bef2e5',width=2)
  elif index in [22,23,24,25]:
   if index==22:d.polygon([(2,20),(5,10),(13,6),(22,14),(19,22)],fill='#687a67');d.line((6,13,13,9,18,13),fill='#9ba78b',width=2)
   if index==23:d.rectangle((10,13,14,24),fill='#866346');d.ellipse((2,1,22,18),fill='#577c56');d.ellipse((4,1,17,12),fill='#7f9b62')
   if index==24:d.rectangle((3,4,21,22),fill='#946d4b',outline='#503d35',width=2);d.line((5,6,19,20),fill='#c8a36b',width=2);d.line((19,6,5,20),fill='#c8a36b',width=2)
   if index==25:d.rectangle((6,3,18,22),fill='#728a8b',outline='#354d54');d.rectangle((5,7,19,9),fill='#a2b7aa');d.rectangle((5,17,19,19),fill='#a2b7aa')
  elif index==26:d.rectangle((11,9,14,24),fill='#696b63');d.polygon([(12,0),(2,16),(8,14),(1,21),(23,21),(17,14),(22,16)],fill='#809d99');d.line((6,11,12,2,17,11),fill='#d2e2db',width=2)
  elif index==27:d.ellipse((3,8,21,22),fill='#75aa9b');d.rectangle((10,9,14,19),fill='#d4efb6');d.rectangle((7,12,17,16),fill='#d4efb6')
  elif index==28:d.rectangle((6,8,18,22),fill='#637d7e');d.rectangle((4,7,20,12),fill='#c0d0b2');d.rectangle((10,1,14,12),fill='#8caeaa')
  elif index==29:d.ellipse((4,6,20,22),fill='#d6b967');d.rectangle((10,9,14,18),fill='#fae1a0')
  elif index==30:
   im=ns['tile'](4);d=ImageDraw.Draw(im);d.rectangle((4,8,20,15),fill='#70a89a');d.ellipse((9,13,15,19),fill='#b8e2a9')
  elif index==31:im=ns['tile'](14)
  atlas.alpha_composite(im,((index%8)*24,(index//8)*24))
 ns['save'](atlas,APP/'assets/extra.bin');atlas.resize((576,360),Image.Resampling.NEAREST).save(APP/'assets/extra-preview.png')
 audio=runpy.run_path(str(ROOT/'tools/build-manor-audio.py'));g=audio['build'].__globals__;g['APP']=APP;original=g['frequency'];g['frequency']=lambda n:original(n-5);audio['build']()
 clips=json.loads((APP/'assets/audio.json').read_text());pcm=bytearray((APP/'assets/sounds.pcm').read_bytes());start=len(pcm);rand=7
 for i in range(720):
  rand=(rand*1664525+1013904223)&0xffffffff;value=(((rand>>16)/32768-1)*.75+math.sin(i*.12)*.25)*5500*(1-i/720)**2;pcm.extend(struct.pack('<h',round(value)))
 clips['shot']={'offset':start,'length':len(pcm)-start};clips['skill']=clips['double'];clips['won']=clips['clear'];clips['dead']=clips['hurt'];clips['boss']=clips['flag'];clips['select']=clips['fruit']
 (APP/'assets/sounds.pcm').write_bytes(pcm);(APP/'assets/audio.json').write_text(json.dumps(clips,separators=(',',':'))+'\n')
 audiojs=(ROOT/'apps/跳跳庄园/audio.js').read_text(encoding='utf-8').replace('createManorAudio','createSurvivorAudio').replace('MANOR_AUDIO','SURVIVOR_AUDIO').replace('跳跳庄园','荒野生存').replace('庄园小调','荒野行军曲')
 (APP/'audio.js').write_text(audiojs,encoding='utf-8')

def build():
 assets();page()
 (APP/'app.js').write_text((ROOT/'tools/app-shared/game-runtime.js').read_text(encoding='utf-8')+'\n'+'\n'.join((APP/name).read_text(encoding='utf-8') for name in ['engine.js','audio.js','game.js']),encoding='utf-8')
 pack(APP,ROOT/'dist/apps/荒野生存.bpk')
if __name__=='__main__':build()
