"""Real-device Doom shareware, PCM, input, reload and cleanup checks."""
import argparse
import hashlib
import json
from pathlib import Path
import time
from device import Device
from native import status
from doom import stop_previous
from importlib import import_module
q=import_module('test-quake2-device')

def audio(d):return {k:int(v) for k,v in (s.split('=') for s in d.request('DBG_QAUDIO').split(','))}

def finish(d):
    d.request('DBG_HOME');end=time.monotonic()+15
    while status(d)['active']:
        if time.monotonic()>end:raise TimeoutError('Doom failed to exit')
        time.sleep(.1)
    s=status(d);stats=q.counters(d)
    assert s['phase']=='cancelled' and s['error']=='ESP_OK',s
    assert stats['allocations']==stats['bytes']==stats['keys']==0,stats
    assert (d.state()['width'],d.state()['height'])==(320,480)
    assert d.request('DBG_CHECK')=='ok=1'
    return s

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--port',default='COM10')
    p.add_argument('--output',type=Path,default=Path('.tools/doom-check'));a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
    with Device(a.port) as d:
        print(d.request('HELLO',timeout=30),flush=True)
        assert d.request('DBG_BUILD')==hashlib.sha256(Path('build/ouse_core_desktop.elf').read_bytes()).hexdigest()
        stop_previous(d);before=d.request('DBG_HEAP');print('BEFORE',before,flush=True)
        d.request('DBG_OPEN','ouse.doom');q.wait_running(d,25);time.sleep(1)
        assert (d.state()['width'],d.state()['height'])==(480,320)
        d.screenshot(a.output/'title.jpg');print('TITLE',q.counters(d),flush=True)
        q.press(d,27,.1)
        for _ in range(3):q.press(d,13,.1)
        time.sleep(5);assert status(d)['active'],status(d)
        d.screenshot(a.output/'map-before.jpg');print('MAP',q.counters(d),flush=True)
        first=audio(d);audio_start=time.monotonic();q.press(d,200,2);last=audio(d)
        audio_elapsed=time.monotonic()-audio_start
        assert last['played']>first['played'] and last['nonzero']>first['nonzero'] and last['peak']>0,(first,last)
        samples_per_second=(last['played']-first['played'])/audio_elapsed
        assert 14000<samples_per_second<18000,('Audio starved during fullscreen rendering',samples_per_second)
        print('AUDIO firing',first,'->',last,'samples/s',samples_per_second,flush=True)
        q.press(d,ord('w'),1);d.request('DBG_QMOUSE','80:0');q.press(d,ord('1'),.1);q.press(d,ord('2'),.1)
        d.screenshot(a.output/'map-after.jpg')
        d.request('DBG_QKEY','119:1');time.sleep(1.1);assert q.counters(d)['keys']==0
        game=q.counters(d);ended=finish(d)
        # Resume the cached desktop app, then close while foreground.
        d.request('DBG_OPEN','ouse.doom');q.wait_running(d,25);time.sleep(.2)
        d.request('DBG_CLOSE','ouse.doom');end=time.monotonic()+15
        while status(d)['active']:
            if time.monotonic()>end:raise TimeoutError('Foreground close failed')
            time.sleep(.1)
        assert q.counters(d)['allocations']==0 and d.request('DBG_CHECK')=='ok=1'
        assert (d.state()['width'],d.state()['height'])==(320,480)
        after=d.request('DBG_HEAP');print('AFTER',after,flush=True)
        result=dict(before=before,after=after,game=game,audio_before=first,audio_after=last,audio_samples_per_second=samples_per_second,stopped=ended,evidence=str(d.output))
        (a.output/'result.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
        print('PASS Doom title/E1M1, firing PCM/codec, WASD/mouse/weapons, input expiry, Home/reopen/close and heap integrity',flush=True)
if __name__=='__main__':main()
