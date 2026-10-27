"""Real camera fill/whole replay in portrait/landscape and both navigation modes."""
import argparse
import hashlib
import json
import re
import runpy
import time
from pathlib import Path

from bpk import Client
from device import Device

ROOT=Path(__file__).resolve().parents[1]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',default='COM10')
    parser.add_argument('--output',type=Path,default=ROOT/'.tools/camera-preview-device')
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    helper=runpy.run_path(str(ROOT/'tools/test-game2d-camera-device.py'))
    probe=helper['make_probe'](args.output);probe_id=helper['PROBE']
    report={'checks':[]}
    with Device(args.port) as d:
        assert 'core='+ (ROOT/'version.txt').read_text().strip() in d.request('HELLO',timeout=30)
        assert d.request('DBG_BUILD')==hashlib.sha256((ROOT/'build/ouse_core_desktop.elf').read_bytes()).hexdigest()
        original=d.state();before=set(d.list_packages());assert probe_id not in before
        assert not original['alive'],'Run while no other apps are open'
        initial_errors=int(re.search(r'errors=(\d+)',d.request('DBG_RUNTIME'))[1])
        installed=False
        def wait(predicate,seconds=30):
            end=time.monotonic()+seconds
            while time.monotonic()<end:
                value=predicate()
                if value:return value
                time.sleep(.2)
            raise TimeoutError('camera device condition; '+str(d.state()))
        def nodes():return d.tree()['nodes']
        def text(value):return any(n.get('text')==value for n in nodes())
        def click(value):
            for _ in range(6):
                n=next((n for n in nodes() if n.get('text')==value),None)
                state=d.state();bottom=state['height']-(40 if state['navigation'] else 24)
                if n:
                    a,b,c,e=n['bounds']
                    if b>=0 and e<bottom-4:
                        d.tap((a+c)//2,(b+e)//2);return
                d.swipe(state['width']//2,bottom-70,state['width']//2,bottom-150,duration_ms=600,hold_ms=300);time.sleep(.3)
            raise AssertionError('Could not scroll control into visible area: '+value)
        def navigation(mode):
            if d.state()['navigation']==mode:return
            orient(False)
            d.request('DBG_HOME')
            if 'ouse.settings' in d.state()['alive']:d.request('DBG_CLOSE','ouse.settings')
            d.request('DBG_OPEN','ouse.settings');wait(lambda:text('外观'));click('外观')
            wait(lambda:text('三键导航'));click('三键导航' if mode else '全面屏手势')
            wait(lambda:d.state()['navigation']==mode);d.request('DBG_HOME');d.request('DBG_CLOSE','ouse.settings')
        def orient(landscape):
            d.request('DBG_HOME');d.request('DBG_OPEN',probe_id);wait(lambda:text('ENGINE:PASS'))
            click('LANDSCAPE' if landscape else 'PORTRAIT');wait(lambda:(d.state()['width']>320)==landscape)
        def ready():
            return any(n.get('radius')==38 and n.get('border_width')==3 and not n.get('disabled') for n in nodes())
        def geometry(fill,nav):
            state=d.state();w,h=state['width'],state['height']-(40 if nav else 0)
            ns=nodes()
            images=[n for n in ns if not n.get('clickable') and n['bounds'][2]-n['bounds'][0]>200 and n['bounds'][3]-n['bounds'][1]>150
                    and abs((n['bounds'][2]-n['bounds'][0]+1)*3-(n['bounds'][3]-n['bounds'][1]+1)*4)<=4]
            assert images,ns
            a,b,c,e=images[0]['bounds']
            if fill:assert a<=0 and b<=0 and c>=w-1 and e>=h-1,(w,h,images)
            else:assert a>=0 and b>=0 and c<w and e<h,(w,h,images)
            # The parent is the clipping viewport; the source image may extend
            # beyond it, including into the navbar's geometric coordinates.
            assert any(n['bounds']==[0,0,w-1,h-1] for n in ns)
            return dict(width=w,height=h,image=images[0]['bounds'])
        try:
            Client.install(d,(ROOT/'dist/bpk/camera.bpk').read_bytes())
            Client.install(d,probe);installed=True
            for nav in (0,1):
                navigation(nav)
                for land in (False,True):
                    orient(land);d.request('DBG_OPEN','user.camera');wait(ready)
                    # A resumed camera retains its display mode; start each
                    # paired inspection in default fill mode.
                    if text('铺满取景'):click('铺满取景')
                    wait(lambda:text('完整画面'));time.sleep(15)
                    tag=('buttons' if nav else 'gestures')+'-'+('landscape' if land else 'portrait')
                    fill=geometry(True,nav);d.screenshot(args.output/(tag+'-fill.jpg'))
                    click('完整画面');wait(lambda:text('铺满取景'));time.sleep(2)
                    whole=geometry(False,nav);d.screenshot(args.output/(tag+'-whole.jpg'))
                    click('铺满取景');wait(lambda:text('完整画面'));wait(ready)
                    report['checks'].append(dict(case=tag,fill=fill,whole=whole))
                    if nav:
                        s=d.state();d.tap(s['width']//2,s['height']-20)
                        wait(lambda:d.state()['active']=='')
                        d.request('DBG_OPEN','user.camera');wait(ready)
            report['runtime']=d.request('DBG_RUNTIME')
            assert int(re.search(r'errors=(\d+)',report['runtime'])[1])==initial_errors
            assert d.request('DBG_CHECK')=='ok=1';report['heapIntegrity']=True
            raw=(d.output/'uart.raw.log').read_text(encoding='utf-8',errors='replace')
            assert 'task_wdt:' not in raw and 'JavaScript execution time exceeded' not in raw
            report['evidence']=str(d.output)
            (args.output/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
        finally:
            d.request('DBG_HOME')
            if 'user.camera' in d.state()['alive']:d.request('DBG_CLOSE','user.camera')
            if installed:
                navigation(original['navigation']);orient(original['width']>original['height'])
                d.request('DBG_HOME');d.request('REMOVE',probe_id)
            report['finalState']=d.state();report['evidence']=str(d.output)
            (args.output/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
        assert set(d.list_packages())==before
    print(json.dumps(report,ensure_ascii=False,indent=2))


if __name__=='__main__':main()
