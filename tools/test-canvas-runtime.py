"""Compile production Canvas/QuickJS code against real LVGL; run pixel QA and benchmark.

Host CPU timings only. No SPI/display-panel timing is simulated or claimed.
"""
from pathlib import Path
import os, subprocess, json, platform, hashlib

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'.tools/canvas-runtime-check'
OUT.mkdir(parents=True,exist_ok=True)
HOST=ROOT/'.tools/ouse-core-check'
RUNTIME=ROOT/'components/ouse_runtime'
QJS=ROOT/'managed_components/espressif__quickjs-ng/quickjs-ng'
source=(RUNTIME/'ouse_runtime.c').read_text(encoding='utf-8')
def section(a,b): return source[source.index(a):source.index(b,source.index(a))]
(OUT/'runtime-types.h').write_text(section('typedef enum { CONTAINER','typedef struct {\n    JSValue handler;'),encoding='utf-8')
(OUT/'sprite-pump.h').write_text(section('static void image_pump(','static void build_nodes('),encoding='utf-8')
env=dict(os.environ)
for line in subprocess.check_output(['cmd.exe','/d','/c',str(HOST/'environment.cmd')],encoding='mbcs').splitlines():
    if '=' in line and not line.startswith('='):
        key,value=line.split('=',1);env[key]=value
compiler=Path(env['VCToolsInstallDir'])/'bin/Hostx64/x64/cl.exe'
qjslib=ROOT/'.tools/ouse-runtime-check/quickjs-native.lib'
assert qjslib.exists() and (HOST/'lvgl-native.lib').exists(), 'Build host libraries with existing runtime/core tests first'
command=[str(compiler),'/nologo','/O2','/utf-8','/std:c11','/MD','/D_CRT_SECURE_NO_WARNINGS',
    '/DLV_KCONFIG_IGNORE','/DLV_LVGL_H_INCLUDE_SIMPLE',
    '/DLV_CONF_PATH="'+(HOST/'lv_conf.h').as_posix()+'"']
command+=['/I'+str(p) for p in [OUT,QJS,ROOT/'managed_components/lvgl__lvgl',HOST,RUNTIME,RUNTIME/'include']]
command += [str(ROOT/'tools/fixtures/canvas-runtime.c'),str(qjslib),str(HOST/'lvgl-native.lib'),
    'Ws2_32.lib','/Fe:'+str(OUT/'check.exe')]
subprocess.run(command,cwd=OUT,env=env,check=True)
shared=(ROOT/'tools/app-shared/canvas.js').read_text(encoding='utf-8')+'\n'+(ROOT/'tools/app-shared/render-benchmark.js').read_text(encoding='utf-8')
(OUT/'scene.js').write_text(shared,encoding='utf-8')
subprocess.run([str(OUT/'check.exe'),str(OUT/'scene.js'),str(OUT/'samples.csv'),
    str(OUT/'canvas.ppm')],cwd=OUT,check=True,timeout=120)
import csv, statistics
rows=list(csv.DictReader((OUT/'samples.csv').open()))
groups={}
for row in rows:
    key=(row['invalidation'],int(row['entities']),row['mode'])
    groups.setdefault(key,[]).append(row)
results=[]
for (policy,count,mode),samples in groups.items():
    entry={'invalidation':policy,'entities':count,'mode':mode,'frames':len(samples)}
    for field in ['prepare_us','submit_us','sync_us','render_us','total_us','flush_pixels']:
        values=sorted(float(row[field]) for row in samples)
        entry[field]={'mean':statistics.mean(values),'p50':statistics.median(values),'p95':values[int(.95*(len(values)-1))]}
    results.append(entry)
tracked=[RUNTIME/'ouse_runtime.c',RUNTIME/'ouse_runtime_canvas.inc',RUNTIME/'ouse_canvas_draw.inc',RUNTIME/'include/ouse_canvas_types.h',ROOT/'tools/app-shared/render-benchmark.js',ROOT/'tools/fixtures/canvas-runtime.c',HOST/'lv_conf.h',qjslib,HOST/'lvgl-native.lib']
report={'sourceSha256':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in tracked},'environment':{'os':platform.platform(),'processor':platform.processor(),'compiler':str(compiler),
    'display':'480x222 RGB565 partial buffer 480x32; flush copies pixels to host RAM',
    'scope':'Actual production QuickJS bindings, sprite pump, Canvas pump/draw; equivalent LVGL object setup/position application. Not complete firmware scheduling or hardware FPS.',
    'warmupFrames':40,'trials':3,'measuredFramesPerTrial':160,
    'pixelParity':'96 initial frames compared pixel by pixel; all 4800 timed/warmup frame pairs checked by 64-bit framebuffer hash outside timing',
    'lvglConfig':str(HOST/'lv_conf.h')},'results':results}
(OUT/'results.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('policy   N   mode      prepare  submit  sync  render  total (mean us)')
for r in results:
    print(f"{r['invalidation']:7} {r['entities']:3} {r['mode']:7} "+' '.join(f"{r[k]['mean']:7.1f}" for k in ['prepare_us','submit_us','sync_us','render_us','total_us']))
print('Evidence:',OUT/'results.json')
