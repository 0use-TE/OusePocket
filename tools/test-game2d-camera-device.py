"""API 48 device smoke: native batches, first/second photo, both orientations,
gallery and Survivor. Installs/removes only its own temporary probe package.
The two actual camera photos are retained as capture evidence.
"""
import argparse,hashlib,json,shutil,time
from pathlib import Path
from bpk import pack,Client
from device import Device

ROOT=Path(__file__).resolve().parents[1]
PROBE='user.test-game2d-camera'
SCRIPT=r'''
let pending=0,offset=0;
function check(ok){if(!ok)throw Error('native device smoke');}
check(Ouse.game2d.version===1);
const a=new Float64Array([0,0,1200,0,1,1,1,0]),b=new Float64Array(32*4),h=new Uint32Array(1);
b.set([10,0,1,1],31*4);Ouse.game2d.stepProjectiles(a.buffer,b.buffer,h.buffer,1/60);
check(a[0]===20&&h[0]===0x80000000);
const x=a[0];a[7]=NaN;let rejected=false;try{Ouse.game2d.stepProjectiles(a.buffer,b.buffer,h.buffer,1/60);}catch(e){rejected=true;}check(rejected&&a[0]===x);
Ouse.ui.setText('result','ENGINE:PASS');
Ouse.ui.on('portrait','click',()=>Ouse.display.setOrientation('portrait'));
Ouse.ui.on('landscape','click',()=>Ouse.display.setOrientation('landscape'));
function list(){if(!pending){Ouse.ui.setText('photos','PHOTOS:WAIT');pending=Ouse.photos.list(offset);check(pending);}}
Ouse.ui.on('list','click',()=>{offset=0;list();});Ouse.ui.on('next','click',()=>{offset+=3;list();});
Ouse.tasks.onComplete(e=>{if(e.id!==pending)return;pending=0;check(e.state==='succeeded');const s=Ouse.photos.get();Ouse.ui.setText('photos','PHOTOS:'+s.offset+':'+s.entries.length);for(let i=0;i<3;i++)Ouse.ui.setText('p'+i,'P'+i+':'+(s.entries[i]?s.entries[i].path:''));});
'''

def make_probe(out):
    folder=out/'probe';folder.mkdir(parents=True,exist_ok=True)
    manifest={'runtime':'ouse-core','api':1,'id':PROBE,'name':'API48 check','icon':'icon.bin','versionCode':1,'versionName':'1.0.0','minNativeApi':48,'capabilities':{'photos':True}}
    children=[dict(type='label',id='result',text='ENGINE:WAIT',x=8,y=8,width=300,height=24)]
    for i,(id,text) in enumerate((('portrait','PORTRAIT'),('landscape','LANDSCAPE'),('list','LIST'))):
        children.append(dict(type='button',id=id,text=text,x=8+i*100,y=42,width=96,height=40))
    children.append(dict(type='button',id='next',text='NEXT',x=8,y=86,width=96,height=30))
    children.append(dict(type='label',id='photos',text='PHOTOS:IDLE',x=112,y=90,width=192,height=24))
    for i in range(3):children.append(dict(type='label',id='p'+str(i),text='P'+str(i)+':',x=8,y=120+i*36,width=300,height=34,fontSize=12))
    page={'api':1,'units':'dp','root':dict(type='container',id='root',width='fill',height='fill',children=children)}
    for name,data in [('manifest.json',manifest),('page.json',page)]:
        (folder/name).write_text(json.dumps(data),encoding='utf-8')
    (folder/'app.js').write_text(SCRIPT,encoding='utf-8');shutil.copyfile(ROOT/'apps/camera/icon.bin',folder/'icon.bin')
    result=out/'probe.bpk';pack(folder,result);return result.read_bytes()

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--port',default='COM10');parser.add_argument('--output',type=Path,default=ROOT/'.tools/game2d-camera-device-check');args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=True);probe=make_probe(args.output)
    report={'captures':[],'checks':[]}
    with Device(args.port) as d:
        assert 'core='+ (ROOT/'version.txt').read_text().strip() in d.request('HELLO',timeout=30)
        assert d.request('DBG_BUILD')==hashlib.sha256((ROOT/'build/ouse_core_desktop.elf').read_bytes()).hexdigest()
        before=set(d.list_packages());assert PROBE not in before
        original=d.state();installed=False
        def texts():return [n['text'] for n in d.tree()['nodes'] if n.get('text')]
        def wait(predicate,timeout=20):
            end=time.monotonic()+timeout
            while time.monotonic()<end:
                value=predicate()
                if value:return value
                time.sleep(.2)
            raise TimeoutError('Device condition not reached; '+str(d.state()))
        def click(text):
            node=next(n for n in d.tree()['nodes'] if n.get('text')==text);x1,y1,x2,y2=node['bounds'];d.tap((x1+x2)//2,(y1+y2)//2)
        def open_probe():
            d.request('DBG_HOME');d.request('DBG_OPEN',PROBE);wait(lambda:'ENGINE:PASS' in texts())
        def photos():
            open_probe();click('LIST');paths=[];offset=0
            while True:
                status=wait(lambda:next((s for s in texts() if s.startswith('PHOTOS:'+str(offset)+':')),None))
                paths.extend(s[3:] for s in texts() if s.startswith(('P0:','P1:','P2:')) and len(s)>3)
                if int(status.split(':')[2])<=3:return paths
                offset+=3;assert offset<=4095;click('NEXT')
        try:
            Client.install(d,probe);installed=True;open_probe();report['checks'].append('native API 48, swept hit bit 31 and atomic rejection')
            initial=set(photos());click('PORTRAIT');wait(lambda:d.state()['width']==320)
            d.request('DBG_OPEN','user.camera')
            def shutter():
                return next((n for n in d.tree()['nodes'] if n.get('clickable') and n.get('border_width')==3 and n.get('radius')==38 and not n.get('disabled')),None)
            first=wait(shutter,30);d.screenshot(args.output/'camera-portrait.jpg')
            for i in range(2):
                n=wait(shutter);x1,y1,x2,y2=n['bounds'];d.tap((x1+x2)//2,(y1+y2)//2)
                time.sleep(1);wait(shutter,30)
                assert not any('失败' in s or '未就绪' in s for s in texts()),texts()
                d.screenshot(args.output/f'capture-{i+1}.jpg')
            after=photos();new=[p for p in after if p not in initial];assert len(new)>=2,(initial,after)
            for path in new[:2]:
                size,crc=d.request('DBG_FINFO',path).split(':');assert int(size)>0;report['captures'].append(dict(path=path,bytes=int(size),crc32=crc))
            report['checks'].append('first and second shutter save distinct nonempty photos')
            click('LANDSCAPE');wait(lambda:d.state()['width']==480);d.request('DBG_OPEN','user.camera');n=wait(shutter,30)
            assert n['bounds'][0]>350,n;d.screenshot(args.output/'camera-landscape.jpg')
            report['checks'].append('portrait bottom controls and landscape right controls')
            d.request('DBG_OPEN','user.gallery');time.sleep(3);d.screenshot(args.output/'gallery.jpg')
            for _ in range(3):d.swipe(240,220,240,75);time.sleep(.5)
            d.screenshot(args.output/'gallery-scrolled.jpg');report['checks'].append('gallery open and scroll without script fault')
            # End the camera/gallery phase and release its native image caches
            # before measuring the separate game's normal foreground session.
            d.request('DBG_HOME')
            for id in ('user.camera','user.gallery',PROBE):d.request('DBG_CLOSE',id)
            d.request('DBG_OPEN','user.survivor');time.sleep(4)
            state=d.state();assert state['width']==480
            def start_ready():
                nodes=d.tree()['nodes']
                return any(n.get('text')=='开始生存' and i>0 and not nodes[i-1].get('disabled') for i,n in enumerate(nodes))
            wait(start_ready);click('开始生存');wait(lambda:b'SURVIVOR_STATE ' in (d.output/'uart.raw.log').read_bytes())
            time.sleep(5);assert d.state()['active']=='user.survivor'
            assert b'SURVIVOR_ENGINE native' in (d.output/'uart.raw.log').read_bytes()
            d.screenshot(args.output/'survivor.jpg');report['checks'].append('updated Survivor starts and runs with native API available')
            report['runtime']=d.request('DBG_RUNTIME');assert 'errors=0' in report['runtime'],report['runtime']
            assert d.request('DBG_CHECK')=='ok=1'
            report['checks'].append('heap integrity and runtime errors=0')
        finally:
            if installed:
                try:
                    current=d.state()
                    if (current['width']>current['height'])!=(original['width']>original['height']):
                        open_probe();click('LANDSCAPE' if original['width']>original['height'] else 'PORTRAIT')
                finally:
                    d.request('DBG_HOME');d.request('REMOVE',PROBE)
                    for id in ('user.camera','user.gallery','user.survivor'):
                        if id in d.state().get('alive',[]):d.request('DBG_CLOSE',id)
            report['finalState']=d.state();report['deviceEvidence']=str(d.output)
            (args.output/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
        assert set(d.list_packages())==before
    print(json.dumps(report,ensure_ascii=False,indent=2))

if __name__=='__main__':main()
