"""Real-device Quake II menu, serial input, timeout and cleanup smoke test."""
import argparse
import hashlib
import json
from pathlib import Path
import time
from device import Device
from native import status

def counters(d):
    return {k:int(v) for k,v in (item.split('=') for item in d.request('DBG_QUAKE').split(','))}

def press(d,key,seconds=.3):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        d.request('DBG_QKEY',f'{key}:1');time.sleep(.1)
    d.request('DBG_QKEY',f'{key}:0');time.sleep(.3)

def wait_running(d,seconds=5):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        s=status(d)
        if not s['active']:raise RuntimeError(str(s))
        if s['phase']=='running' and counters(d)['frames']>=5:return
        time.sleep(.25)
    raise TimeoutError('No game frames')

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--port',default='COM10')
    p.add_argument('--audio',action='store_true',help='Require engine PCM and codec consumption')
    p.add_argument('--output',type=Path,default=Path('.tools/quake-check'))
    a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
    with Device(a.port) as d:
        print(d.request('HELLO',timeout=30),flush=True)
        firmware=Path(__file__).resolve().parents[1]/'build/ouse_core_desktop.elf'
        assert d.request('DBG_BUILD')==hashlib.sha256(firmware.read_bytes()).hexdigest(),'Firmware hash mismatch'
        d.request('DBG_HOME');d.request('DBG_CLOSE','ouse.native');d.request('DBG_CLOSE','ouse.quake2')
        end=time.monotonic()+15
        while status(d)['active']:
            if time.monotonic()>end:raise TimeoutError('Previous game failed to stop')
            time.sleep(.1)
        before=d.request('DBG_HEAP');print('BEFORE',before,flush=True)
        d.request('DBG_OPEN','ouse.quake2');wait_running(d,20)
        screen=d.state()
        assert (screen['width'],screen['height'])==(480,320),screen
        d.screenshot(a.output/'menu.jpg');print('MENU',counters(d),flush=True)
        # Down/up back-to-back must survive even inside one render frame.
        d.request('DBG_QKEY','13:1');d.request('DBG_QKEY','13:0');time.sleep(.5)
        d.request('DBG_QKEY','13:1');d.request('DBG_QKEY','13:0')
        time.sleep(15)
        if not status(d)['active']:raise RuntimeError(str(status(d)))
        d.screenshot(a.output/'map-before.jpg');print('MAP',counters(d),flush=True)
        if a.audio:
            def audio():return {k:int(v) for k,v in (item.split('=') for item in d.request('DBG_QAUDIO').split(','))}
            first=audio();press(d,200,2);last=audio()
            assert last['played']>first['played'] and last['written']>first['written'],(first,last)
            assert last['nonzero']>first['nonzero'] and last['peak']>0,(first,last)
            print('AUDIO firing:',first,'->',last,flush=True)
            (a.output/'audio.json').write_text(json.dumps(dict(before=first,after=last),indent=2))
        press(d,131,1)
        d.request('DBG_QMOUSE','80:0');time.sleep(.5)
        d.screenshot(a.output/'map-after.jpg')
        d.request('DBG_QKEY','128:1');time.sleep(.2)
        assert counters(d)['keys']>=1
        time.sleep(1)
        assert counters(d)['keys']==0,'Lost input heartbeat must release held keys'
        moving=counters(d);print('INPUT',moving,flush=True)
        d.request('DBG_HOME')
        end=time.monotonic()+15
        while status(d)['active']:
            if time.monotonic()>end:raise TimeoutError('Game failed to stop')
            time.sleep(.1)
        ended=status(d);stats=counters(d)
        screen=d.state()
        assert (screen['width'],screen['height'])==(320,480),screen
        assert ended['phase']=='cancelled' and ended['error']=='ESP_OK',ended
        assert stats['allocations']==stats['bytes']==stats['keys']==0,stats
        d.request('DBG_CLOSE','ouse.quake2');after=d.request('DBG_HEAP');print('AFTER',after,flush=True)
        assert d.request('DBG_CHECK')=='ok=1'
        result=dict(before=before,after=after,game=moving,stopped=ended,resources=stats,evidence=str(d.output))
        (a.output/'result.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
        print('PASS menu/map capture, serial key/mouse, idle key release, Home stop and heap integrity',flush=True)

if __name__=='__main__':main()
