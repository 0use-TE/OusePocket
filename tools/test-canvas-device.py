"""Install both benchmark BPKs and capture real panel timings on a connected board.

Requires firmware Native API 42, CONFIG_OUSE_DEVICE_DEBUG and an explicit --port.
Does not flash firmware or publish packages. Both benchmark apps request landscape.
"""
from pathlib import Path
from datetime import datetime
import argparse, json, re, subprocess, sys, time
from device import Device, ProtocolError
from bpk import Client, pack

ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--port',required=True)
parser.add_argument('--seconds',type=int,default=240,help='Maximum capture duration per app; stop after all four stages complete')
args=parser.parse_args()
if args.seconds<60:parser.error('--seconds must be >=60')
subprocess.run([sys.executable,str(ROOT/'tools/generate-render-benchmark.py')],check=True,cwd=ROOT)
output=ROOT/'.tools/diagnostics'/('canvas-device-'+datetime.now().strftime('%Y%m%d-%H%M%S'))
output.mkdir(parents=True,exist_ok=False)
for mode in ('canvas','json'):pack(ROOT/'apps'/('benchmark-'+mode),output/(mode+'.bpk'))
for mode in ('canvas','json'):
    app='user.benchmark-'+mode
    with Device(args.port,output/mode,ROOT/'build/ouse_core_desktop.elf') as d:
        hello=d.request('HELLO')
        # Existing HELLO fields vary across protocol revisions. The installer
        # also enforces minNativeApi=42 before loading the package.
        before=d.request('DBG_RUNTIME');errors=int(re.search(r'errors=(\d+)',before)[1])
        try:d.request('DBG_CLOSE',app)
        except ProtocolError as error:
            if 'ESP_ERR_NOT_FOUND' not in str(error):raise
        Client.install(d,(output/(mode+'.bpk')).read_bytes())
        try:
            d.request('DBG_OPEN',app)
            completed=False
            for second in range(args.seconds):
                time.sleep(1)
                if second in (8,20,35,50):d.screenshot(output/mode/('scene-'+str(second)+'.jpg'))
                raw=(output/mode/'uart.raw.log').read_text(encoding='utf-8',errors='replace')
                if re.search(r'BENCH_SUBMIT[^\r\n]*"count":96[,}]',raw):
                    completed=True;break
        finally:
            d.request('DBG_CLOSE',app);d.request('DBG_HOME')
        after=d.request('DBG_RUNTIME')
        assert completed,'Not all four benchmark stages completed within the time limit'
        assert int(re.search(r'errors=(\d+)',after)[1])==errors,after
        assert d.request('DBG_CHECK')=='ok=1'
        (output/mode/'result.json').write_text(json.dumps({'hello':hello,'before':before,'after':after,
            'seconds':second+1,'completedAllStages':True,'note':'BENCH_SUBMIT logs are JS submission timing; OuseDisplay logs include render phase, flush waits and bytes. Neither is alone complete game FPS.'},indent=2),encoding='utf-8')
print('Device evidence:',output)
