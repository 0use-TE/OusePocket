"""Install and control the experimental Quake II runtime over USB serial."""
import argparse
from pathlib import Path
import time
from device import Device
from native import upload_memory, status

ROOT=Path(__file__).resolve().parents[1]
KEYS={"Return":13,"Escape":27,"BackSpace":127,"Tab":9,"space":32,
      "Up":128,"Down":129,"Left":130,"Right":131,"Control_L":133,
      "Control_R":133,"Shift_L":134,"Shift_R":134,"Alt_L":132,"Alt_R":132,
      "grave":96,"asciitilde":126,"comma":44,"period":46,"slash":47,"backslash":92,
      "bracketleft":91,"bracketright":93,"minus":45,"equal":61,"plus":43,"apostrophe":39}
KEYS.update({f"F{i}":134+i for i in range(1,13)})

def play(d):
    import tkinter as tk
    root=tk.Tk();root.title("OusePocket Quake II — USB 串口控制")
    root.geometry("620x340")
    tk.Label(root,text="点击此窗口，用电脑键鼠控制设备上的游戏",font=("Microsoft YaHei UI",15)).pack(pady=22)
    tk.Label(root,text="方向键移动 / 鼠标左键射击 / Space 跳跃\nEnter 确认 / Esc 菜单 / ~ 控制台\n关闭窗口会松开所有按键，游戏继续运行。",font=("Microsoft YaHei UI",12)).pack(pady=8)
    message=tk.StringVar(value="请保持设备已进入游戏。")
    tk.Label(root,textvariable=message,wraplength=580).pack(pady=12)
    held=set();last=[None];pending=[0,0];failed=[False]
    def send(verb,payload="-"):
        if failed[0]:return
        try:d.request(verb,payload,timeout=2,attempts=1)
        except Exception as e:
            failed[0]=True;held.clear();message.set(str(e))
    def code(event):
        if event.keysym in KEYS:return KEYS[event.keysym]
        c=event.keysym.lower()
        return ord(c) if len(c)==1 and 32<=ord(c)<127 else None
    def key(event,down):
        k=code(event)
        if k is None:return
        if down and k not in held:held.add(k);send("DBG_QKEY",f"{k}:1")
        elif not down and k in held:held.remove(k);send("DBG_QKEY",f"{k}:0")
    def button(event,down):
        k=200 if event.num==1 else 201
        if down:held.add(k)
        else:held.discard(k)
        send("DBG_QKEY",f"{k}:{int(down)}")
    def mouse(event):
        if last[0]:pending[0]+=event.x-last[0][0];pending[1]+=event.y-last[0][1]
        last[0]=(event.x,event.y)
    def release(event=None):
        held.clear();last[0]=None;pending[:]=[0,0];send("DBG_QFREE")
    def tick():
        if not failed[0]:
            dx=max(-1024,min(1024,pending[0]));dy=max(-1024,min(1024,pending[1]))
            pending[0]-=dx;pending[1]-=dy
            # Refresh every held key, including key 0 as an idle heartbeat.
            for k in tuple(held):send("DBG_QKEY",f"{k}:1")
            send("DBG_QMOUSE",f"{dx}:{dy}")
        root.after(100,tick)
    root.bind("<KeyPress>",lambda e:key(e,True));root.bind("<KeyRelease>",lambda e:key(e,False))
    for n in (1,3):
        root.bind(f"<ButtonPress-{n}>",lambda e:button(e,True))
        root.bind(f"<ButtonRelease-{n}>",lambda e:button(e,False))
    root.bind("<Motion>",mouse);root.bind("<Leave>",lambda e:last.__setitem__(0,None))
    root.bind("<FocusOut>",release)
    root.protocol("WM_DELETE_WINDOW",lambda:(release(),root.destroy()))
    tick();root.mainloop()

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("command",choices=("install","start","stop","status","play","data"))
    p.add_argument("argument",nargs="?")
    p.add_argument("--port",default="COM10")
    p.add_argument("--no-wait",action="store_true",help="Return after submitting a data download")
    a=p.parse_args()
    with Device(a.port) as d:
        print(d.request("HELLO",timeout=30))
        if a.command=="install":
            print(upload_memory(d,Path(a.argument or ROOT/"dist/native/quake2.elf"),install="quake2"))
            end=time.monotonic()+30
            while status(d)["active"]:
                if time.monotonic()>end:raise TimeoutError("Installation pending")
                time.sleep(.1)
            s=status(d);print(s)
            if s["phase"]!="installed":raise RuntimeError("Install failed")
        elif a.command=="data":
            if not a.argument:p.error("data requires an HTTP URL for the original demo pak0.pak")
            print(d.request("DBG_QDATA",a.argument))
            end=time.monotonic()+20*60+30
            while not a.no_wait:
                progress=d.request("DBG_QDATA");print(progress,flush=True)
                state=dict(item.split("=",1) for item in progress.split(","))
                if state["phase"]=="succeeded":break
                if state["phase"] in ("failed","cancelled"):raise RuntimeError(progress)
                if time.monotonic()>end:raise TimeoutError("Resource download still pending")
                time.sleep(2)
        elif a.command=="start":
            d.request("DBG_HOME")
            d.request("DBG_CLOSE","ouse.native")
            d.request("DBG_CLOSE","ouse.quake2")
            end=time.monotonic()+15
            while status(d)["active"]:
                if time.monotonic()>end:raise TimeoutError("Previous native session is still running")
                time.sleep(.1)
            d.request("DBG_OPEN","ouse.quake2")
            end=time.monotonic()+15
            while True:
                s=status(d)
                if not s["active"]:raise RuntimeError(str(s))
                if s["phase"]=="running":break
                if time.monotonic()>end:raise TimeoutError("Game startup is still pending")
                time.sleep(.1)
        elif a.command=="stop":d.request("DBG_ELFSTOP")
        elif a.command=="play":play(d)
        print(status(d));print(d.request("DBG_QUAKE"));print("Evidence:",d.output.resolve())

if __name__=="__main__":main()
