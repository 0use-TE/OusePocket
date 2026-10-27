"""Paired on-board JS/native Survivor replay, using production engine and renderer.

Installs only a temporary BPK; does not replace the user's game or firmware.
Game-loop FPS measures completed UI-paced callbacks, not physical LCD scanout.
"""
import argparse
import hashlib
import json
import re
import shutil
import statistics
import time
from pathlib import Path

from bpk import Client, pack
from device import Device

ROOT = Path(__file__).resolve().parents[1]
APP_ID = 'user.bench-survivor'

HARNESS = r'''
BENCH_NATIVE=true;const nativeGame=createSurvivorEngine(12345);BENCH_NATIVE=false;const jsGame=createSurvivorEngine(12345);
const runs=[];
for(let rep=0;rep<3;rep++)for(const count of [8,24])for(const cap of [25,0])
 for(const native of (rep%2?[true,false]:[false,true]))runs.push({rep,count,cap,native});
let runIndex=0,benchLoop=null,shotRestore=[],samples=[],simMs=0,renderMs=0,frame=0,started=0,previous=0;
const checks={};
function rewindShots(){const dx=Math.sin(frame*.04)*24,dy=Math.cos(frame*.04)*24;for(const p of shotRestore){const b=p[0];b.on=true;b.x=p[1]+dx;b.y=p[2]+dy;b.life=1.7;b.mask=0;b.pierce=100;}game.drainEvents();}
function setup(run){
 BENCH_NATIVE=run.native;game=run.native?nativeGame:jsGame;game.reset({hero:0,gun:3,pet:1,zone:0});
 const s=game.state;s.spawn=10000;s.fire=10000;s.hero.invincible=10000;s.waveTime=10000;
 for(let i=0;i<run.count;i++){
  const a=i*2*Math.PI/run.count,f=game.spawn(0);
  Object.assign(f,{x:640+Math.cos(a)*100,y:480+Math.sin(a)*100,speed:0,hp:1e6,maxHp:1e6,clock:10000});
 }
 const shots=run.count===8?8:32;
 for(let i=0;i<shots;i++){
  const f=s.foes[i%run.count],a=Math.atan2(f.y-480,f.x-640);
  Object.assign(s.shots[i],{on:true,x:f.x-Math.cos(a)*4,y:f.y-Math.sin(a)*4,dx:Math.cos(a)*350,dy:Math.sin(a)*350,
   life:1.7,damage:0,gun:3,pierce:100,mask:0});
 }
 shotRestore=s.shots.filter(b=>b.on).map(b=>[b,b.x,b.y]);phase='game';ready=true;active=true;sync();camera(true);hudKey='';
 frame=0;samples=[];simMs=renderMs=0;started=previous=0;
}
function next(){
 if(benchLoop)benchLoop.stop();
 if(runIndex===runs.length){BENCH_FINISHED=true;Ouse.ui.show('home');Ouse.ui.setText('menuTitle','BENCH DONE');Ouse.display.setOrientation(BENCH_ORIENTATION);console.log('BENCH_DONE');return;}
 const run=runs[runIndex++];setup(run);
 console.log('BENCH_START '+JSON.stringify(run));
 // Measure full simulation including packing/unpacking; repeated restores are
 // excluded from the timed region and identical across both backends.
 let batch=0,total=0;
 function compute(){
  rewindShots();const t=Date.now();game.advance(1/60);total+=Date.now()-t;
  if(++batch<120){Ouse.timers.setTimeout(compute,33);return;}
  console.log('BENCH_COMPUTE '+JSON.stringify({...run,steps:120,ms:total}));
  benchLoop=createGameRuntime(()=>{
   const now=Date.now();
   if(frame>=30){if(!started){started=now;previous=now;}else samples.push(now-previous);previous=now;}
   rewindShots();const t=Date.now();game.advance(1/60);const u=Date.now();camera(true);render();renderHud();const v=Date.now();
   if(frame>=30){simMs+=u-t;renderMs+=v-u;}
   frame++;
   if(frame===210){
    benchLoop.stop();Ouse.timers.setTimeout(()=>{
    const finalKey=run.count+':'+run.cap+':'+run.rep,finalState=JSON.stringify(game.state);
    if(checks[finalKey]){if(checks[finalKey]!==finalState)throw Error('BENCH parity mismatch');delete checks[finalKey];}else checks[finalKey]=finalState;
    const ordered=samples.slice().sort((a,b)=>a-b);
    console.log('BENCH_RESULT '+JSON.stringify({...run,backend:game.backend,frames:180,
     p50:ordered[89],p95:ordered[170],max:ordered[178],fps:179000/(now-started),simMs,renderMs,checksumMatch:true}));
    Ouse.timers.setTimeout(next,100);
    },100);
   }
  },run.cap);benchLoop.start();
 }Ouse.timers.setTimeout(compute,100);
}
requestScreen();layout();Ouse.timers.setTimeout(next,1500);
globalThis.app={onPause(){if(benchLoop)benchLoop.stop();},onResume(){},onClose(){closed=true;if(benchLoop)benchLoop.stop();}};
'''


def make_package(out, orientation):
    source = ROOT / 'apps/survivor'
    folder = out / 'app'
    folder.mkdir(parents=True, exist_ok=True)
    for name in ('page.json', 'icon.bin'):
        shutil.copyfile(source / name, folder / name)
    shutil.copytree(source / 'assets', folder / 'assets', dirs_exist_ok=True)
    manifest = json.loads((source / 'manifest.json').read_text(encoding='utf-8'))
    manifest.update(id=APP_ID, name='Survivor A/B benchmark', names={}, versionCode=1)
    manifest['assets'] = [asset for asset in manifest['assets'] if asset['id'] in ('actors', 'extra')]
    (folder / 'manifest.json').write_text(json.dumps(manifest), encoding='utf-8')
    engine = (source / 'engine.js').read_text(encoding='utf-8')
    assert engine.count("const native=typeof Ouse") == 1
    engine = engine.replace("const native=typeof Ouse", "const native=BENCH_NATIVE&&typeof Ouse")
    game = (source / 'game.js').read_text(encoding='utf-8')
    game = game.replace('createGameRuntime(tick,0)', 'createGameRuntime(tick)')
    game = game.replace('const game=createSurvivorEngine(),content=game.content,sound=createSurvivorAudio(Ouse),loop=createGameRuntime(tick);',
                        "let game=createSurvivorEngine();const content=game.content,sound={sync(){},update(){},effect(){},close(){}},loop=createGameRuntime(tick);")
    assert 'let game=createSurvivorEngine()' in game
    game = game.replace('function requestScreen(){', 'function requestScreen(){if(BENCH_FINISHED)return;')
    game = game[:game.index('function tick(){')]
    game = game[:game.index('function start(){')] + game[game.index('function requestScreen(){'):]
    game = game.replace('loop=createGameRuntime(tick)', 'loop={fps:0}')
    script = 'let BENCH_NATIVE=false,BENCH_FINISHED=false;const BENCH_ORIENTATION='+json.dumps(orientation)+';\n' + (ROOT / 'tools/app-shared/game-runtime.js').read_text(encoding='utf-8') + '\n' + engine + '\n' + game + '\n' + HARNESS
    (folder / 'app.js').write_text(script, encoding='utf-8')
    target = out / 'benchmark.bpk'
    pack(folder, target)
    return target.read_bytes()


def parse_records(raw, prefix):
    records = []
    for line in raw.splitlines():
        if prefix in line:
            records.append(json.loads(line.split(prefix, 1)[1].rstrip('\r\n\x1b[0m')))
    return records


def summarize(report, raw):
    telemetry = []
    current = None
    compute_at = None
    points = []
    for line in raw.splitlines():
        timestamp = re.search(r'\((\d+)\)', line)
        if not timestamp:
            continue
        stamp = int(timestamp[1])
        if 'BENCH_START ' in line:
            current = json.loads(line.split('BENCH_START ', 1)[1])
            compute_at, points = None, []
        elif 'BENCH_COMPUTE ' in line:
            compute_at = stamp
        elif compute_at is not None and stamp >= compute_at + 2500 and 'OuseLCDFrame: async frames=' in line:
            points.append((stamp, int(re.search(r'async frames=(\d+)', line)[1])))
        elif 'BENCH_RESULT ' in line and current is not None:
            if len(points) >= 2:
                elapsed, frames = points[-1][0]-points[0][0], points[-1][1]-points[0][1]
                telemetry.append(dict(**current, elapsedMs=elapsed, frames=frames, fps=frames*1000/elapsed))
            compute_at = None
    groups = []
    for count in (8, 24):
        for cap in (25, 0):
            group = dict(enemies=count, shots=8 if count == 8 else 32, cap=cap)
            for native in (False, True):
                def matching(rows):
                    return [r for r in rows if r['count'] == count and r['cap'] == cap and r['native'] == native]
                rows = matching(report['records'])
                computes = matching(report['compute'])
                lcd = matching(telemetry)
                group['native' if native else 'js'] = dict(
                    fps=statistics.median(r['fps'] for r in rows),
                    fpsRange=[min(r['fps'] for r in rows), max(r['fps'] for r in rows)],
                    simMsPerStep=statistics.median(r['simMs']/r['frames'] for r in rows),
                    isolatedSimMs=statistics.median(r['ms']/r['steps'] for r in computes),
                    renderSubmitMs=statistics.median(r['renderMs']/r['frames'] for r in rows),
                    p95FrameMs=statistics.median(r['p95'] for r in rows),
                    lcdFps=sum(r['frames'] for r in lcd)*1000/sum(r['elapsedMs'] for r in lcd) if lcd else None,
                    lcdSampleMs=sum(r['elapsedMs'] for r in lcd))
            group['fpsGainPercent'] = (group['native']['fps']/group['js']['fps']-1)*100
            group['simTimeReductionPercent'] = (1-group['native']['simMsPerStep']/group['js']['simMsPerStep'])*100
            groups.append(group)
    return dict(groups=groups, lcdTelemetry=telemetry,
                lcdMethod='counter differences strictly within active replay, >=2.5s after compute phase; weighted by sample duration; asynchronous 5s telemetry')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='COM10')
    parser.add_argument('--output', type=Path, default=ROOT / '.tools/survivor-device-benchmark')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    report = {'method': 'seeded stationary enemies and moving projectile replay; production simulation and renderer; 30 warmup + 180 measured frames; 3 alternating repetitions; audio disabled equally; 1 ms timer resolution', 'records': []}
    with Device(args.port) as device:
        assert 'core='+ (ROOT/'version.txt').read_text().strip() in device.request('HELLO', timeout=30)
        report['firmwareElfSha256'] = device.request('DBG_BUILD')
        assert report['firmwareElfSha256'] == hashlib.sha256((ROOT / 'build/ouse_core_desktop.elf').read_bytes()).hexdigest()
        before = set(device.list_packages())
        assert APP_ID not in before
        original = device.state()
        report['originalState'] = original
        report['runtimeBefore'] = device.request('DBG_RUNTIME')
        errors_before = int(re.search(r'errors=(\d+)', report['runtimeBefore'])[1])
        package = make_package(args.output, 'landscape' if original['width'] > original['height'] else 'portrait')
        device.request('DBG_HOME')
        for app_id in original.get('alive', []):
            device.request('DBG_CLOSE', app_id)
        installed = False
        try:
            Client.install(device, package)
            installed = True
            device.request('DBG_OPEN', APP_ID)
            end = time.monotonic() + 900
            count = 0
            while time.monotonic() < end:
                raw = (device.output / 'uart.raw.log').read_text(encoding='utf-8', errors='replace')
                rows = parse_records(raw, 'BENCH_RESULT ')
                report['records'] = rows
                report['compute'] = parse_records(raw, 'BENCH_COMPUTE ')
                if len(rows) > count:
                    for row in rows[count:]:
                        print(json.dumps({k: v for k, v in row.items() if k != 'intervals'}), flush=True)
                    count = len(rows)
                if 'BENCH_DONE' in raw:
                    break
                if 'BENCH parity mismatch' in raw:
                    raise AssertionError('Backend state differs')
                if any(s in raw for s in ('JavaScript execution time exceeded', 'Uncaught', ': RangeError:', ': TypeError:', ': ReferenceError:')):
                    raise AssertionError('Benchmark script failed; see raw UART evidence')
                time.sleep(1)
            else:
                raise TimeoutError('Benchmark did not complete')
            assert len(rows) == 24, len(rows)
            report['records'] = rows
            report['compute'] = parse_records(raw, 'BENCH_COMPUTE ')
            device.screenshot(args.output / 'completed.jpg')
            report['runtime'] = device.request('DBG_RUNTIME')
            assert int(re.search(r'errors=(\d+)', report['runtime'])[1]) == errors_before, report['runtime']
            assert device.request('DBG_CHECK') == 'ok=1'
        finally:
            if installed:
                # The successful run restores orientation before BENCH_DONE.
                device.request('DBG_HOME')
                device.request('REMOVE', APP_ID)
            for app_id in original.get('alive', []):
                device.request('DBG_OPEN', app_id)
            if original.get('active'):
                device.request('DBG_OPEN', original['active'])
            else:
                device.request('DBG_HOME')
            report['finalState'] = device.state()
            report['evidence'] = str(device.output)
            (args.output / 'result.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
        assert set(device.list_packages()) == before
    raw = (Path(report['evidence']) / 'uart.raw.log').read_text(encoding='utf-8', errors='replace')
    report['summary'] = summarize(report, raw)
    (args.output / 'result.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    for count in (8, 24):
        for cap in (25, 0):
            pair = {}
            for native in (False, True):
                rows = [r for r in report['records'] if r['count'] == count and r['cap'] == cap and r['native'] == native]
                pair['native' if native else 'js'] = dict(fps=statistics.median(r['fps'] for r in rows),
                    simMsPerStep=statistics.median(r['simMs']/r['frames'] for r in rows),
                    renderSubmitMs=statistics.median(r['renderMs']/r['frames'] for r in rows))
            print(json.dumps(dict(enemies=count, shots=8 if count == 8 else 32, cap=cap, **pair)), flush=True)


if __name__ == '__main__':
    main()
