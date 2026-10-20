"""Build first-stage restoration assets from original FFDec output and AS3 data."""
from pathlib import Path
import hashlib,json,re,struct,subprocess
from PIL import Image,ImageOps
ROOT=Path(__file__).resolve().parents[1];SRC=ROOT/'.tools/game-47931';APP=ROOT/'apps/brave-road'
def sprite(name,n=1):return Image.open(next((SRC/'sprites').glob('*_'+name+'/'+str(n)+'.png'))).convert('RGBA')
def save(im,name):
    im.save(APP/'assets'/(name+'.png'))
    (APP/'assets'/(name+'.bin')).write_bytes(b'OIC1'+struct.pack('<HH',*im.size)+im.tobytes('raw','BGRA'))
sheet=Image.new('RGBA',(1128,2748))
for name in ['pics1','picb','pich','picd1','picws1']:sheet.alpha_composite(sprite(name))
indices=[0,1,2,3,4,5,6,7,8,12,13,14,15,16,17,22]
hero=Image.new('RGBA',(24*16,28*2))
for i,n in enumerate(indices):
    cell=sheet.crop((n%4*282,n//4*229,n%4*282+282,n//4*229+229))
    # Fixed pivot: original character origin (125,161) maps to (12,27).
    cell=cell.resize((56,46),Image.Resampling.LANCZOS)
    frame=Image.new('RGBA',(24,28));frame.paste(cell,(-13,-5))
    hero.alpha_composite(frame,(i*24,0));hero.alpha_composite(ImageOps.mirror(frame),(i*24,28))
save(hero,'hero')
icon=hero.crop((0,0,24,28)).resize((52,52),Image.Resampling.LANCZOS)
(APP/'icon.bin').write_bytes(b'OIC1'+struct.pack('<HH',52,52)+icon.tobytes('raw','BGRA'))
for kind in ['l1','l2','l3']:
    atlas=Image.new('RGBA',(18*17,22*2))
    for n in range(1,18):
        # FFDec uses a consistent full timeline bounding box across frames.
        im=sprite(kind+'mc',n);im.thumbnail((18,22),Image.Resampling.LANCZOS)
        frame=Image.new('RGBA',(18,22));frame.alpha_composite(im,((18-im.width)//2,22-im.height))
        atlas.alpha_composite(frame,((n-1)*18,0));atlas.alpha_composite(ImageOps.mirror(frame),((n-1)*18,22))
    save(atlas,kind)
save(sprite('lv1f').resize((96,48),Image.Resampling.LANCZOS),'sky')
save(sprite('lv1n').resize((192,46),Image.Resampling.LANCZOS),'village')
sd_images=SRC/'sd-images';sd_images.mkdir(exist_ok=True)
for name,source_name,size in [('sky','lv1f',(930,330)),('village','lv1n',(1620,385))]:
    high=sprite(source_name).resize(size,Image.Resampling.LANCZOS)
    high.save(sd_images/('brave-road-'+name+'-hq-v1.png'))
    (sd_images/('brave-road-'+name+'-hq-v1.bin')).write_bytes(b'OIC1'+struct.pack('<HH',*size)+high.tobytes('raw','BGRA'))
save(sprite('bg1').resize((16,16),Image.Resampling.LANCZOS),'ground')
itemnames=['hps','hpm','exps','expm','s2','d2','ws2','wd2']
items=Image.new('RGBA',(12*len(itemnames),12))
for i,n in enumerate(itemnames):
    im=sprite(n+'_i');im=im.crop(im.getbbox());im.thumbnail((12,12),Image.Resampling.LANCZOS)
    items.alpha_composite(im,(i*12+(12-im.width)//2,12-im.height))
save(items,'items')
source=(SRC/'clean-export/scripts/roles/enemys/littlerole1.as').read_text(encoding='utf-8')
monsters={}
for n in ['l1','l2','l3']:
    body=source.split('case "'+n+'":',1)[1].split('break;',1)[0]
    values={k:int(re.search(r'\b'+k+r'\s*=\s*(-?\d+)\s*;',body)[1]) for k in ['thisAllHp','thisAtt','thisDef','thisSpeed','jumpSpeed','thisExp','defHitSpd']}
    for k in ['beHitLeft','beHitRight','beHitTop','beHitBottom','searchX','searchY','escapeX','escapeY']:
        values[k]=int(re.search(r'\b'+k+r'\s*=\s*(-?\d+)\s*;',body)[1])
    values['drops']=json.loads(re.search(r'thisItemArr\s*=\s*(\[[^;]+\]);',body)[1])
    special=re.search(r'thisSItemArr\s*=\s*(\[[^;]+\]);',body);values['rareDrops']=json.loads(special[1]) if special else []
    monsters[n]=values
levelsource=(SRC/'clean-export/scripts/levelDate.as').read_text(encoding='utf-8')
match=re.search(r'public var lvDate1:Object\s*=\s*',levelsource);stage,_=json.JSONDecoder().raw_decode(levelsource[match.end():])
(APP/'assets/first-level.json').write_text(json.dumps(stage,separators=(',',':')),encoding='utf-8')
bank=bytearray();sounds={}
ff=ROOT/'.tools/media-deps/imageio_ffmpeg/binaries/ffmpeg-win-x86_64-v7.1.exe'
for name in ['att1','att2','att3','behit1','behit2','lvup','get1']:
    sourcefile=next((SRC/'export/sounds').glob('*_'+name+'.mp3'))
    pcm=subprocess.check_output([str(ff),'-v','error','-i',str(sourcefile),'-ar','16000','-ac','1','-f','s16le','-'])
    sounds[name]={'offset':len(bank),'length':len(pcm),'source':sourcefile.name,'sha256':hashlib.sha256(pcm).hexdigest()};bank.extend(pcm)
(APP/'assets/sounds.pcm').write_bytes(bank)
sd_audio=SRC/'sd-audio';sd_audio.mkdir(exist_ok=True)
for name,rate in [('music1',8000),('win',16000),('lose',16000)]:
    sourcefile=next((SRC/'export/sounds').glob('*_'+name+'.mp3'))
    subprocess.run([str(ff),'-v','error','-y','-i',str(sourcefile),'-ar',str(rate),'-ac','1','-c:a','pcm_s16le',str(sd_audio/('brave-road-'+name+'-v1.wav'))],check=True)
rules={'fps':24,'scale':.5,'monsters':monsters,'hero':{'hpBase':150,'hpPerLevel':30,'hpPerDress':120,'mpBase':50,'mpPerLevel':10,'swordBaseAttack':10,'swordAttackPerWeapon':15,'shieldBaseDefense':10,'shieldDefensePerItem':10,'expPerLevel':50,'maxLevel':40,'speed':5,'jumpSpeed':-30,'gravity':3,'protectFrames':20},'sounds':sounds,'itemNames':itemnames,'stageEnemies':sum(len(g[3]) for g in stage['enemyArr'])}
(APP/'assets/rules.json').write_text(json.dumps(rules,separators=(',',':')),encoding='utf-8')
images=['hero','l1','l2','l3','sky','village','ground','items']
manifest=json.loads((APP/'manifest.json').read_text(encoding='utf-8'))
manifest.update(minNativeApi=44,description='First-stage single-player restoration: original three monster types, 28-enemy waves, stats, village layers and audio. Touch control adaptation.',capabilities={'media':True,'files':True})
manifest['assets']=[{'id':n,'type':'image','path':'assets/'+n+'.bin'} for n in images]+[{'id':'first-level','type':'text','path':'assets/first-level.json'},{'id':'rules','type':'text','path':'assets/rules.json'},{'id':'sounds','type':'binary','path':'assets/sounds.pcm'}]
(APP/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
page=json.loads((APP/'page.json').read_text(encoding='utf-8'));children=page['root']['children']
for n in children:
    if n['id']=='status':n.clear();n.update(type='canvas',id='hud',x=0,y=0,width=240,height=30)
    if n['id']=='modal':
        for c in n['children']:
            if c['id']=='title':c['text']='勇者之路 · 第一关'
            if c['id']=='stage':c['text']='声音：开'
            if c['id']=='info':c['text']='左侧摇杆 / 右侧攻击与跳跃\n顶部四技能 / 单人触屏适配'
(APP/'page.json').write_text(json.dumps(page,ensure_ascii=False,separators=(',',':')),encoding='utf-8')
if (APP/'game.js').exists():
    (APP/'app.js').write_text((APP/'hud.js').read_text(encoding='utf-8')+'\n'+(APP/'game.js').read_text(encoding='utf-8'),encoding='utf-8')
print('image bytes',sum((APP/'assets'/(n+'.bin')).stat().st_size-8 for n in images),'sound bytes',len(bank),'first-stage monsters',rules['stageEnemies'])
