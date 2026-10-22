"""Install and control Doom on OusePocket using the native game bridge."""
import argparse
from pathlib import Path
import time
from device import Device
from native import upload_memory,status
from quake2 import play
ROOT=Path(__file__).resolve().parents[1]

def stop_previous(d):
    d.request('DBG_HOME')
    for app in ('ouse.native','ouse.quake2','ouse.doom'):d.request('DBG_CLOSE',app)
    end=time.monotonic()+15
    while status(d)['active']:
        if time.monotonic()>end:raise TimeoutError('Previous native session is still stopping')
        time.sleep(.1)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('command',choices=('install','start','stop','status','play','data'))
    p.add_argument('argument',nargs='?');p.add_argument('--port',default='COM10')
    a=p.parse_args()
    with Device(a.port) as d:
        print(d.request('HELLO',timeout=30),flush=True)
        if a.command=='install':
            stop_previous(d)
            print(upload_memory(d,Path(a.argument or ROOT/'dist/native/doom.elf'),install='doom'),flush=True)
            end=time.monotonic()+30
            while status(d)['active']:
                if time.monotonic()>end:raise TimeoutError('Doom installation pending')
                time.sleep(.1)
            if status(d)['phase']!='installed':raise RuntimeError(status(d))
        elif a.command=='start':
            stop_previous(d);d.request('DBG_OPEN','ouse.doom')
            end=time.monotonic()+20
            while True:
                s=status(d)
                if not s['active']:raise RuntimeError(s)
                if s['phase']=='running':break
                if time.monotonic()>end:raise TimeoutError('Doom startup pending')
                time.sleep(.1)
        elif a.command=='stop':d.request('DBG_ELFSTOP')
        elif a.command=='play':play(d,title='Doom',doom=True)
        elif a.command=='data':
            if not a.argument:p.error('data requires an HTTP URL for doom1.wad')
            print(d.request('DBG_DDATA',a.argument),flush=True)
            end=time.monotonic()+20*60+30
            while True:
                progress=d.request('DBG_DDATA');print(progress,flush=True)
                state=dict(item.split('=',1) for item in progress.split(','))
                if state['phase']=='succeeded':break
                if state['phase'] in ('failed','cancelled'):raise RuntimeError(progress)
                if time.monotonic()>end:raise TimeoutError('WAD download pending')
                time.sleep(2)
        print(status(d));print(d.request('DBG_QUAKE'));print(d.request('DBG_QAUDIO'))
        print('Evidence:',d.output.resolve())
if __name__=='__main__':main()
