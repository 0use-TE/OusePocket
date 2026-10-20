"""Bake locally extracted SWF resources into an OusePocket single-player prototype.

Input is the local FFDec extraction under .tools/game-47931. No publishing.
"""
from pathlib import Path
import hashlib,json,re,struct
from PIL import Image,ImageOps,ImageDraw

ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'.tools/game-47931'
APP=ROOT/'apps/brave-road'
(APP/'assets').mkdir(parents=True,exist_ok=True)
def sprite(name,frame=1):
    return Image.open(next((SOURCE/'sprites').glob('*_'+name+'/'+str(frame)+'.png'))).convert('RGBA')
def save(im,name):
    im.save(APP/'assets'/(name+'.png'))
    (APP/'assets'/(name+'.bin')).write_bytes(b'OIC1'+struct.pack('<HH',*im.size)+im.tobytes('raw','BGRA'))

# Original creatMc uses 282x229 cells, composed at a common origin.
sheet=Image.new('RGBA',(1128,2748))
for layer in ['pics1','picb','pich','picd1','picws1']:sheet.alpha_composite(sprite(layer),(0,0))
indices=[0,2,4,6,8,12,13,14]
frames=[sheet.crop((i%4*282,i//4*229,i%4*282+282,i//4*229+229)) for i in indices]
# Keep a fixed character foot pivot across all action frames.
box=(65,45,225,180)
hero=Image.new('RGBA',(32*8,32*2))
for i,im in enumerate(frames):
    im=im.crop(box).resize((32,32),Image.Resampling.LANCZOS)
    hero.alpha_composite(im,(i*32,0));hero.alpha_composite(ImageOps.mirror(im),(i*32,32))
save(hero,'hero')
enemy=Image.new('RGBA',(24*4,24*2))
for i,n in enumerate([1,4,7,10]):
    im=sprite('l1mc',n);im=im.crop(im.getbbox());im.thumbnail((24,24),Image.Resampling.LANCZOS)
    cell=Image.new('RGBA',(24,24));cell.alpha_composite(im,((24-im.width)//2,24-im.height))
    enemy.alpha_composite(cell,(i*24,0));enemy.alpha_composite(ImageOps.mirror(cell),(i*24,24))
save(enemy,'enemy')
save(sprite('lv1f').resize((128,64),Image.Resampling.LANCZOS),'sky')
save(sprite('bg1').resize((16,16),Image.Resampling.LANCZOS),'ground')
# Extract the genuine stage collision/enemy/exit data, after trap removal.
s=(SOURCE/'clean-export/scripts/levelDate.as').read_text(encoding='utf-8')
levels=[]
for number in range(1,14):
    match=re.search(r'public var lvDate'+str(number)+r':Object\s*=\s*',s)
    data,end=json.JSONDecoder().raw_decode(s[match.end():])
    levels.append(data)
for group in range(7):
    (APP/'assets'/('levels'+str(group)+'.json')).write_text(json.dumps(levels[group*2:group*2+2],separators=(',',':')),encoding='utf-8')
icon=frames[0].crop(box).resize((80,80),Image.Resampling.LANCZOS)
(APP/'icon.bin').write_bytes(b'OIC1'+struct.pack('<HH',80,80)+icon.tobytes('raw','BGRA'))
manifest={'runtime':'ouse-core','api':1,'id':'user.brave-road','name':'Brave Road Local',
    'names':{'zh_CN':'勇者之路·本地适配','en':'Brave Road Local'},'icon':'icon.bin',
    'versionCode':1,'versionName':'0.1.0','minNativeApi':42,
    'description':'Local single-player adaptation prototype. Extracted artwork and stage data; rewritten gameplay. Not the complete original game.',
    'assets':[{'id':n,'type':'image','path':'assets/'+n+'.bin'} for n in ['hero','enemy','sky','ground']]+
             [{'id':'levels'+str(i),'type':'text','path':'assets/levels'+str(i)+'.json'} for i in range(7)]}
(APP/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
page={'api':1,'units':'dp','root':{'type':'container','id':'page','width':'fill','height':'fill','children':[
    {'type':'canvas','id':'world','width':'fill','height':190,'style':{'backgroundColor':'#182d2a'}},
    {'type':'label','id':'status','x':4,'y':0,'width':220,'height':24,'fontSize':12,'text':'本地单人适配'},
    {'type':'button','id':'pause','x':250,'y':0,'width':64,'height':30,'text':'菜单'},
    {'type':'container','id':'modal','x':10,'y':32,'width':300,'height':186,'style':{'backgroundColor':'#243d40','radius':12},'children':[
        {'type':'label','id':'title','x':10,'y':8,'width':280,'height':30,'text':'勇者之路 · 单人适配'},
        {'type':'label','id':'info','x':10,'y':40,'width':280,'height':48,'fontSize':12,'text':'拖动移动 · 向上滑跳跃\n靠近敌人自动攻击 · 点击冲击波'},
        {'type':'button','id':'play','x':10,'y':96,'width':132,'height':36,'text':'开始冒险'},
        {'type':'button','id':'stage','x':150,'y':96,'width':132,'height':36,'text':'切换关卡'},
        {'type':'button','id':'restart','x':10,'y':140,'width':272,'height':36,'text':'重新开始'}]}]}}
(APP/'page.json').write_text(json.dumps(page,ensure_ascii=False,separators=(',',':')),encoding='utf-8')
metadata={'page':'https://www.4399.com/flash/47931_1.htm','swf':'https://sda.4399.com/4399swf/upload_swf/ftp3/haibo/20110124/1.swf',
    'sha256':hashlib.sha256((SOURCE/'original.swf').read_bytes()).hexdigest(),'tool':'JPEXS FFDec 26.4.0',
    'extraction':{'images':1459,'scripts':413,'sounds':24},'levels':len(levels),
    'imageBytes':sum((APP/'assets'/(n+'.bin')).stat().st_size-8 for n in ['hero','enemy','sky','ground']),
    'limitations':['Rewritten gameplay, not execution of original ActionScript.',
    'Original map layouts retained; first biome sky/ground and mushroom enemy artwork reused across stages.',
    'Reduced action frames; no original equipment/pet/skill/story/audio systems.',
    'Not a public-domain assertion; source page attributes copyright to the author. Local files only.']}
(APP/'provenance.json').write_text(json.dumps(metadata,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(metadata,ensure_ascii=True,indent=2))
