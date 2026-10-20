"""Verify both firmware touch paths using the installed API Test multi-touch page.
Leaves the page ready for physical two-finger verification; no package/data removal.
"""
import argparse,time
from device import Device

def labels(d):return [n['text'] for n in d.tree()['nodes'] if n.get('text')]
def tap_text(d,part):
 n=next(n for n in d.tree()['nodes'] if part in n.get('text',''));b=n['bounds'];d.tap((b[0]+b[2])//2,(b[1]+b[3])//2);time.sleep(.2)
def contact(d,slot,x,y,down):return d.request('DBG_TOUCHES',f'{slot}:{x}:{y}:{int(down)}')
def count(d,n):
 text=labels(d);assert any(t.startswith(f'{n} / 2') for t in text),text;return text

def exercise(d,name):
 state=d.state();w,h=state['width'],state['height'];y=145;x0=w//4;x1=w*3//4
 try:
  contact(d,0,x0,y,True);time.sleep(.08);contact(d,1,x1,y,True);time.sleep(.2)
  print(name,'two',count(d,2),flush=True);d.screenshot(str(d.output/(name+'-two.jpg')))
  contact(d,1,x1,y,False);time.sleep(.15);print(name,'one',count(d,1),flush=True)
  contact(d,0,x0+15,y+15,True);time.sleep(.08);contact(d,0,w-2,h-30,False);time.sleep(.2);count(d,0)
  contact(d,1,x1,y,True);contact(d,0,x0,y,True);time.sleep(.15);count(d,2)
  d.request('DBG_HOME');time.sleep(.3);d.request('DBG_OPEN','user.api-test');time.sleep(.3);count(d,0)
  # Release physical leases after UI cancellation; no stale down may reappear.
 finally:
  contact(d,0,0,0,False);contact(d,1,0,0,False)
 time.sleep(.2);count(d,0)
 print(name,'PASS simultaneous/independent/outside release/background cancel',flush=True)

def main():
 p=argparse.ArgumentParser();p.add_argument('--port',default='COM10');a=p.parse_args()
 with Device(a.port) as d:
  print(d.request('HELLO'),flush=True)
  d.request('DBG_CLOSE','user.api-test');d.request('DBG_OPEN','user.api-test');time.sleep(.8)
  tap_text(d,'控件与输入');tap_text(d,'Multi-touch');exercise(d,'portrait')
  # Existing landscape game requests the real public orientation API; no game input.
  d.request('DBG_OPEN','user.survivor');time.sleep(2);d.request('DBG_OPEN','user.api-test');time.sleep(.4)
  assert d.state()['width']>d.state()['height'];exercise(d,'landscape')
  stats=d.request('DBG_RUNTIME');print('runtime',stats,flush=True);assert 'errors=0' in stats
  print('heap',d.request('DBG_HEAP'),flush=True);print('evidence',d.output,flush=True)
 print('PASS; serial closed, both contacts released',flush=True)
if __name__=='__main__':main()
