"""Exercise NES keyboard/button ownership, heartbeat and disconnect cleanup."""
from types import SimpleNamespace as Event
import tkinter as tk
import nes

original_tk=tk.Tk
original_button=tk.Button
buttons={}
errors=[]
checkpoints=[]

class Button(original_button):
    def __init__(self,*args,**kwargs):
        self.handlers={};super().__init__(*args,**kwargs);buttons[kwargs['text']]=self
    def bind(self,sequence=None,func=None,add=None):
        if func:self.handlers[sequence]=func
        return super().bind(sequence,func,add)

class Window(original_tk):
    def __init__(self):
        self.handlers={};super().__init__();self.withdraw()
    def mainloop(self,*args):
        self.after(20,lambda:self.handlers['<KeyPress>'](Event(keysym='z')))
        self.after(40,lambda:buttons['A'].handlers['<ButtonPress-1>'](None))
        self.after(60,lambda:self.handlers['<KeyRelease>'](Event(keysym='z')))
        self.after(240,lambda:checkpoints.append(list(device.events)))
        self.after(260,lambda:buttons['A'].handlers['<ButtonRelease-1>'](None))
        self.after(280,lambda:self.handlers['<KeyPress>'](Event(keysym='Up')))
        self.after(300,lambda:self.handlers['<FocusOut>'](None))
        self.after(320,lambda:self.handlers['<KeyPress>'](Event(keysym='Return')))
        self.after(340,lambda:self.handlers['<KeyPress>'](Event(keysym='Escape')))
        return super().mainloop(*args)
    def bind(self,sequence=None,func=None,add=None):
        if func:self.handlers[sequence]=func
        return super().bind(sequence,func,add)
    def report_callback_exception(self,*error):
        errors.append(error);self.destroy()

class Serial:
    def __init__(self,fail=False):self.events=[];self.fail=fail
    def request(self,verb,payload='-',**kwargs):
        self.events.append((verb,payload))
        if self.fail:raise OSError('Simulated disconnect')
        return '-'

try:
    tk.Tk=Window;tk.Button=Button
    device=Serial();nes.play(device)
    assert not errors,errors
    assert checkpoints[0].count(('DBG_QKEY','122:1'))>=2,'Held button needs heartbeat'
    assert ('DBG_QKEY','122:0') not in checkpoints[0],'Keyboard release must preserve mouse-held A'
    assert ('DBG_QKEY','122:0') in device.events
    assert ('DBG_QKEY','128:1') in device.events and ('DBG_QKEY','13:1') in device.events
    assert device.events[-1]==('DBG_QFREE','-'),device.events
    assert device.events.count(('DBG_QFREE','-'))>=2,'Focus loss and close must release inputs'
    device=Serial(fail=True);nes.play(device)
    assert not errors,errors
    assert len(device.events)==1,'Disconnect must stop subsequent input requests'
finally:
    tk.Tk=original_tk;tk.Button=original_button
print('PASS NES keyboard/buttons, shared ownership, heartbeat, focus/close and disconnect cleanup')
