"""Summarize completed hardware stages; discard display windows crossing stages."""
from pathlib import Path
import argparse, hashlib, json, re

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('evidence',type=Path)
parser.add_argument('--output',type=Path)
args=parser.parse_args()
rows=[]
for mode in ('canvas','json'):
    folder=args.evidence/mode
    state=json.loads((folder/'result.json').read_text(encoding='utf-8'))
    assert state['completedAllStages'],mode
    text=(folder/'uart.raw.log').read_text(encoding='utf-8',errors='replace')
    text=re.sub(r'\x1b\[[0-9;]*m','',text)
    stages={};completions={};windows=[];previous=None
    for line in text.splitlines():
        marker=re.search(r'\((\d+)\).*BENCH_(STAGE|SUBMIT) (\{.*)',line)
        if marker:
            value=json.JSONDecoder().raw_decode(marker[3])[0]
            if value['mode']==mode:
                value['uptimeMs']=int(marker[1])
                (stages if marker[2]=='STAGE' else completions).setdefault(value['count'],value)
        timing=re.search(r'\((\d+)\) OuseDisplay: 5s: (.*)',line)
        if timing:
            end=int(timing[1]);values={k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',timing[2])}
            if previous is not None:windows.append(dict(startMs=previous,endMs=end,**values))
            previous=end
    for count in (8,32,64,96):
        start=stages[count];end=completions[count]
        chosen=[w for w in windows if w['startMs']>=start['uptimeMs'] and w['endMs']<=end['uptimeMs']]
        assert chosen,(mode,count,'No complete display measurement window')
        seconds=sum((w['endMs']-w['startMs'])/1000 for w in chosen)
        renders=sum(w['renders'] for w in chosen)
        rows.append({'mode':mode,'entities':count,'width':start['width'],'height':start['height'],
            'logicFrames':end['frames'],'stageSeconds':(end['uptimeMs']-start['uptimeMs'])/1000,
            'logicUpdatesPerSecond':end['frames']*1000/(end['uptimeMs']-start['uptimeMs']),
            'jsPrepareSubmitMeanMs':end['meanMs'],'jsPrepareSubmitMaxMs':end['maxMs'],
            'displayWindows':len(chosen),'displayWindowSeconds':seconds,'displayRefreshesPerSecond':renders/seconds,
            'renderPhaseMeanMs':sum(w['phase_avg_us']*w['renders'] for w in chosen)/renders/1000,
            'renderPhaseMaxMs':max(w['phase_max_us'] for w in chosen)/1000,
            'flushWaitPerRefreshMs':sum(w['flush_wait_us'] for w in chosen)/renders/1000,
            'submittedBytesPerRefresh':sum(w['submitted_bytes'] for w in chosen)/renders,
            'submittedBytesPerSecond':sum(w['submitted_bytes'] for w in chosen)/seconds,
            'windows':chosen})
report={'evidence':str(args.evidence.resolve()),'elfSha256':hashlib.sha256((args.evidence/'canvas/ouse_core_desktop.elf').read_bytes()).hexdigest() if (args.evidence/'canvas/ouse_core_desktop.elf').exists() else None,
    'notes':['Real board; 480x222 game scene, 480x320 physical display, RGB565 SPI.',
    'One complete run per mode; each stage has 200 deterministic updates. Timing includes occasional diagnostic screenshots.',
    'Logic update rate and display refresh rate are different metrics; no claim of unique panel scanout frames.',
    'LVGL render phase includes flush waits on the current display adapter; do not add flush-wait time to render-phase time.',
    'Display windows crossing stage boundaries are excluded. Submitted bytes include the rest of the screen and matching HUD.'],
    'results':rows}
target=args.output or args.evidence/'summary.json'
target.write_text(json.dumps(report,indent=2),encoding='utf-8')
print('N mode    logic/s display/s render_phase_ms bytes/refresh')
for row in rows:print(f"{row['entities']:2} {row['mode']:6} {row['logicUpdatesPerSecond']:7.2f} {row['displayRefreshesPerSecond']:9.2f} {row['renderPhaseMeanMs']:15.2f} {row['submittedBytesPerRefresh']:13.0f}")
print('Evidence:',target)
