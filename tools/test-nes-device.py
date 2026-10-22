"""Real NES emulation: ROM integrity, pad edges, music, failures and cleanup."""
import argparse
import hashlib
import json
from pathlib import Path
import time
from importlib import import_module
from PIL import Image,ImageChops
from device import Device,ProtocolError
from native import status
from game_session import stop_previous
from nes import upload_rom
q=import_module('test-quake2-device')

def audio(d):return {k:int(v) for k,v in (x.split('=') for x in d.request('DBG_QAUDIO').split(','))}

def open_nes(d):
    previous=status(d)['taskId'];d.request('DBG_OPEN','ouse.nes');end=time.monotonic()+20
    while True:
        s=status(d)
        if s['taskId']!=previous:
            if not s['active']:raise RuntimeError(s)
            if s['phase']=='running' and q.counters(d)['frames']>=5:return
        if time.monotonic()>end:raise TimeoutError('No new NES frames')
        time.sleep(.1)

def finish(d):
    d.request('DBG_HOME');end=time.monotonic()+15
    while status(d)['active']:
        if time.monotonic()>end:raise TimeoutError('NES failed to stop')
        time.sleep(.1)
    s=status(d);c=q.counters(d)
    assert s['error']=='ESP_OK' and s['phase']=='cancelled',s
    assert c['allocations']==c['bytes']==c['keys']==0,c
    assert (d.state()['width'],d.state()['height'])==(320,480)
    assert d.request('DBG_CHECK')=='ok=1'
    return s

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--port',default='COM10')
    p.add_argument('--output',type=Path,default=Path('.tools/nes-check'));a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
    with Device(a.port) as d:
        print(d.request('HELLO',timeout=30),flush=True)
        assert d.request('DBG_BUILD')==hashlib.sha256(Path('build/ouse_core_desktop.elf').read_bytes()).hexdigest()
        stop_previous(d);d.request('DBG_NROM','Downloads/2048.nes')
        before=d.request('DBG_HEAP');print('BEFORE',before,flush=True)
        open_nes(d);assert (d.state()['width'],d.state()['height'])==(480,320)
        d.screenshot(a.output/'title.jpg');print('TITLE',q.counters(d),flush=True)
        # Down/up submitted inside one render period still reaches the NES pad.
        d.request('DBG_QKEY','13:1');d.request('DBG_QKEY','13:0');time.sleep(1)
        d.screenshot(a.output/'board-before.jpg')
        first=audio(d);start=time.monotonic();time.sleep(2);last=audio(d)
        rate=(last['played']-first['played'])/(time.monotonic()-start)
        assert last['nonzero']>first['nonzero'] and last['peak']>0,(first,last)
        assert 14000<rate<18000,('NES must keep supplying 16kHz audio',rate)
        print('AUDIO',first,'->',last,'samples/s',rate,flush=True)
        for key in (131,129,130,128,131,129):q.press(d,key,.08)
        d.screenshot(a.output/'board-after.jpg')
        with Image.open(a.output/'board-before.jpg') as x,Image.open(a.output/'board-after.jpg') as y:
            assert ImageChops.difference(x.convert('RGB'),y.convert('RGB')).getbbox(),'Pad input did not change the game board'
        d.request('DBG_QKEY','119:1');time.sleep(1.2);assert q.counters(d)['keys']==0
        try:d.request('DBG_NROM','Downloads/2048.nes');raise AssertionError('Must reject ROM changes while game runs')
        except ProtocolError as e:assert 'ESP_ERR_INVALID_STATE' in str(e)
        game=q.counters(d);ended=finish(d);d.request('DBG_CLOSE','ouse.nes')
        # A valid magic with missing declared PRG/CHR must fail without running
        # the CPU or crashing the firmware. Then the normal ROM must still load.
        bad=a.output/'truncated.nes';bad.write_bytes(b'NES\x1a'+bytes([1,1])+bytes(10))
        remote=upload_rom(d,bad);d.request('DBG_NROM',remote)
        previous=status(d)['taskId'];d.request('DBG_OPEN','ouse.nes');end=time.monotonic()+15
        while True:
            s=status(d)
            if s['taskId']!=previous and not s['active']:break
            if time.monotonic()>end:raise TimeoutError('Invalid ROM did not finish')
            time.sleep(.1)
        assert s['phase']=='failed' and s['exitCode']==2,s
        assert q.counters(d)['allocations']==0 and d.request('DBG_CHECK')=='ok=1'
        d.request('DBG_HOME');d.request('DBG_CLOSE','ouse.nes');d.request('DBG_NROM','Downloads/2048.nes')
        open_nes(d);finish(d);d.request('DBG_CLOSE','ouse.nes')
        after=d.request('DBG_HEAP');print('AFTER',after,flush=True)
        result=dict(before=before,after=after,game=game,audio_before=first,audio_after=last,
                    samples_per_second=rate,stopped=ended,invalid_rom=s,evidence=str(d.output))
        (a.output/'result.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
        print('PASS NES title/gameplay, quick Start edge, pad/music/16kHz, key expiry, ROM rejection/recovery, fullscreen and cleanup',flush=True)
if __name__=='__main__':main()
