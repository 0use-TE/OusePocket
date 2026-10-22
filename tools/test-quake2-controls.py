"""Check raw mouse decoding, cursor cleanup, and controller wire events."""
import ctypes as C
from ctypes import wintypes as W
from types import SimpleNamespace as Event
import tkinter as tk
import quake2
import quake2_mouse

for key in 'wasd1234567890':
    assert quake2.key_code(Event(keysym=key,keycode=0))==ord(key)
assert quake2.key_code(Event(keysym='exclam',keycode=49))==49
assert quake2.key_code(Event(keysym='KP_4',keycode=100))==52
assert C.sizeof(quake2_mouse.Mouse)==24

# Install/uninstall the real HWND hook without capturing the user's cursor.
root=tk.Tk();root.withdraw();moves=[];errors=[]
raw=quake2_mouse.RawMouse(root,lambda x,y:moves.append((x,y)),errors.append)
api=raw.api
packet=quake2_mouse.Packet();packet.header.kind=0;packet.header.size=C.sizeof(packet)
packet.mouse.dx=-513;packet.mouse.dy=127
clip=[];positions=[]
class NativeStub:
    def GetRawInputData(self,handle,command,data,size,header):
        size._obj.value=C.sizeof(packet)
        if data is None:return 0
        C.memmove(data,C.byref(packet),C.sizeof(packet));return C.sizeof(packet)
    def GetForegroundWindow(self):return 1
    def GetAncestor(self,hwnd,kind):return 1
    def GetClientRect(self,hwnd,rect):
        rect._obj.right=660;rect._obj.bottom=340;return 1
    def ClientToScreen(self,hwnd,point):return 1
    def GetCursorPos(self,point):point._obj.x=17;point._obj.y=23;return 1
    def ClipCursor(self,rect):
        clip.append(None if rect is None else (rect._obj.left,rect._obj.top,rect._obj.right,rect._obj.bottom));return 1
    def SetCursorPos(self,x,y):positions.append((x,y));return 1
try:
    raw.api=NativeStub();raw._read(1)
    assert moves==[(-513,127)]
    packet.mouse.flags=1;raw._read(1)
    assert moves==[(-513,127)],'Absolute positions must not become relative motion'
    assert raw.capture() and raw.captured
    assert clip[-1]==(330,170,331,171)
    raw.release();assert not raw.captured and clip[-1] is None and positions[-1]==(17,23)
finally:
    raw.api=api;raw.close();root.destroy()

# Exercise the actual Tk controller callbacks and timing without taking focus
# or clipping the real cursor. Verify protocol values rather than GUI internals.
original_tk=tk.Tk;original_mouse=quake2_mouse.RawMouse
instances=[];devices=[]
class CapturedMouse:
    def __init__(self,root,move,fail):
        self.captured=False;self.move=move;self.closed=False;instances.append(self)
    def capture(self):self.captured=True;return True
    def release(self):self.captured=False
    def close(self):self.release();self.closed=True
class Window(original_tk):
    def __init__(self):
        self.handlers={};super().__init__();self.withdraw()
        self.after(20,lambda:self.handlers['<ButtonPress-1>'](Event(num=1)))
        self.after(40,lambda:self.handlers['<KeyPress>'](Event(keysym='w',keycode=87)))
        self.after(60,lambda:self.handlers['<KeyPress>'](Event(keysym='a',keycode=65)))
        self.after(80,lambda:self.handlers['<KeyPress>'](Event(keysym='exclam',keycode=49)))
        self.after(90,lambda:self.handlers['<KeyRelease>'](Event(keysym='exclam',keycode=49)))
        self.after(100,lambda:instances[-1].move(2050,-1025))
        self.after(240,lambda:self.handlers['<KeyPress>'](Event(keysym='F8')))
        self.after(260,lambda:self.handlers['<KeyRelease>'](Event(keysym='F8')))
        self.after(280,lambda:self.handlers['<KeyPress>'](Event(keysym='F8')))
        self.after(300,lambda:self.handlers['<KeyRelease>'](Event(keysym='F8')))
        self.after(320,lambda:self.handlers['<FocusOut>'](None))
        self.after(350,self.destroy)
    def bind(self,sequence=None,func=None,add=None):
        if func:self.handlers[sequence]=func
        return super().bind(sequence,func,add)
class Serial:
    def __init__(self,fail=False):self.events=[];self.fail=fail;devices.append(self)
    def request(self,verb,payload='-',**kwargs):
        self.events.append((verb,payload))
        if self.fail and verb=='DBG_QMOUSE':raise OSError('Simulated disconnect')
        return '-'
try:
    tk.Tk=Window;quake2_mouse.RawMouse=CapturedMouse
    d=Serial();quake2.play(d)
    for key in (119,97,49):assert ('DBG_QKEY',f'{key}:1') in d.events,d.events
    assert ('DBG_QKEY','49:0') in d.events
    deltas=[tuple(map(int,p.split(':'))) for v,p in d.events if v=='DBG_QMOUSE']
    assert (sum(x for x,y in deltas),sum(y for x,y in deltas))==(2050,-1025),deltas
    assert all(abs(x)<=1024 and abs(y)<=1024 for x,y in deltas)
    assert ('DBG_QFREE','-') in d.events and instances[-1].closed and not instances[-1].captured
    d=Serial(fail=True);quake2.play(d)
    assert instances[-1].closed and not instances[-1].captured,'Disconnect must release the cursor'
finally:
    tk.Tk=original_tk;quake2_mouse.RawMouse=original_mouse
print('PASS Win32 hook, relative counts, clip cleanup, WASD/digits, delta splitting, F8/focus/disconnect release')
