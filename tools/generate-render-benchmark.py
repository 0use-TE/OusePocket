"""Generate matching JSON-sprite/Canvas demo packages from shared scene code."""
from pathlib import Path
import json, struct

ROOT = Path(__file__).resolve().parents[1]
shared = (ROOT/'tools/app-shared/render-benchmark.js').read_text(encoding='utf-8')

def atlas():
    pixels = bytearray()
    colors = [(240, 100, 60), (80, 210, 140), (80, 140, 240), (230, 190, 70)]
    for y in range(32):
        for x in range(32):
            r,g,b = colors[(y//16)*2+x//16]
            a = 0 if x%16 in (0,15) or y%16 in (0,15) else 128 if x%16 < 4 else 255
            pixels += bytes((b,g,r,a))
    # Package image format: width/height followed by little-endian ARGB8888.
    return b'OIC1' + struct.pack('<HH',32,32) + pixels

for mode in ('canvas','json'):
    folder=ROOT/'apps'/('benchmark-'+mode)
    (folder/'assets').mkdir(parents=True,exist_ok=True)
    (folder/'assets/actors.bin').write_bytes(atlas())
    # Self-contained deterministic icon, outside the timed scene.
    icon=bytearray()
    for y in range(80):
        for x in range(80):
            icon+=bytes((100,180,70,255)) if 16<=x<64 and 16<=y<64 else bytes((32,32,24,255))
    (folder/'icon.bin').write_bytes(b'OIC1'+struct.pack('<HH',80,80)+icon)
    manifest={'runtime':'ouse-core','api':1,'id':'user.benchmark-'+mode,
        'name':'Render benchmark '+mode,'icon':'icon.bin','versionCode':1,
        'versionName':'1.0.0','minNativeApi':42,'author':'OusePocket',
        'description':'Deterministic matching Canvas/JSON-sprite benchmark. 8/32/64/96 actors.',
        'assets':[{'id':'actors','type':'image','path':'assets/actors.bin'}]}
    children=[{'type':'label','id':'status','x':4,'y':0,'width':312,'height':28,'text':mode}]
    field={'type':'canvas' if mode=='canvas' else 'container','id':'world','x':0,'y':34,
        'width':320,'height':222,'style':{'backgroundColor':'#182020'}}
    if mode=='json':
        field['children']=[{'type':'sprite','id':'e'+str(i),'x':0,'y':0,'width':16,
            'height':16,'src':'actors','frameWidth':16,'frameHeight':16,
            'frameCount':4,'frame':i%4,'playing':False} for i in range(96)]
    children.append(field)
    page={'api':1,'units':'dp','root':{'type':'container','id':'root','width':'fill','height':'fill','children':children}}
    (folder/'manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    (folder/'page.json').write_text(json.dumps(page,separators=(',',':')),encoding='utf-8')
    script='"use strict";\n'+shared+'\nconst MODE='+json.dumps(mode)+';\n'+r'''
let timer=0, frame=0, stage=0, width=320, height=222, total=0, maximum=0;
const counts=[8,32,64,96];
function configure() {
    const d=Ouse.display.get();
    if(d.orientation!=='landscape'){Ouse.display.setOrientation('landscape');return;}
    width=Math.floor(d.contentWidthDp);height=Math.floor(d.contentHeightDp)-34;
    Ouse.ui.layout([{id:'world',x:0,y:34,width,height},{id:'status',x:4,y:0,width:width-8,height:28}]);
    frame=0;total=maximum=0;
    if(MODE==='json')for(let i=0;i<96;i++){
        if(i<counts[stage])Ouse.ui.show('e'+i);else Ouse.ui.hide('e'+i);
    }
    console.log('BENCH_STAGE '+JSON.stringify({mode:MODE,count:counts[stage],width,height,timeMs:Date.now()}));
}
function tick() {
    const started=Date.now();
    benchmarkSubmit(MODE,benchmarkScene(counts[stage],frame,width,height));
    const ms=Date.now()-started;total+=ms;maximum=Math.max(maximum,ms);frame++;
    if(frame%20===0)Ouse.ui.setText('status',MODE+' N='+counts[stage]+' submit avg '+(total/frame).toFixed(1)+' max '+maximum+'ms');
    if(frame===200){
        console.log('BENCH_SUBMIT '+JSON.stringify({mode:MODE,count:counts[stage],frames:frame,meanMs:total/frame,maxMs:maximum}));
        if(stage===counts.length-1){stop();return;}
        stage=(stage+1)%counts.length;configure();
    }
}
function start(){if(!timer)timer=Ouse.timers.setInterval(tick,50);}
function stop(){if(timer)Ouse.timers.clear(timer);timer=0;}
globalThis.app={onCreate(){configure();start();},onResume(){configure();start();},onPause:stop,onClose:stop};
Ouse.display.onChange(configure);
configure();start();
'''
    (folder/'app.js').write_text(script,encoding='utf-8')
print('Generated apps/benchmark-canvas and apps/benchmark-json (shared scene, 4-frame alpha atlas).')
