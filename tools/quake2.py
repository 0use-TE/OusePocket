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

def key_code(event):
    # Windows virtual digits stay stable while Shift is held; support numpad.
    vk=getattr(event,"keycode",0)
    if 0x30<=vk<=0x39:return vk
    if 0x60<=vk<=0x69:return vk-0x60+ord("0")
    if event.keysym in KEYS:return KEYS[event.keysym]
    c=event.keysym.lower()
    return ord(c) if len(c)==1 and 32<=ord(c)<127 else None


def play(d):
    import tkinter as tk
    from quake2_mouse import RawMouse
    root=tk.Tk();root.title("OusePocket Quake II — USB 串口控制")
    root.geometry("660x340")
    tk.Label(root,text="点击窗口捕获鼠标 · F8 释放／重新捕获",font=("Microsoft YaHei UI",15)).pack(pady=22)
    tk.Label(root,text="W / S 前后移动 · A / D 左右平移\n鼠标移动视角 · 左键射击 · Space 跳跃\n1～9 / 0 切换武器 · Enter 确认 · Esc 游戏菜单",font=("Microsoft YaHei UI",12)).pack(pady=8)
    message=tk.StringVar(value="鼠标未捕获。点击窗口后开始控制，F8 可释放鼠标。")
    tk.Label(root,textvariable=message,wraplength=620).pack(pady=12)
    held=set();pending=[0,0];failed=[False];f8_held=[False]
    heartbeat=[0.0];mouse_sent=[0.0];capture=None
    def fail(error):
        failed[0]=True;held.clear();pending[:]=[0,0]
        if capture:capture.release()
        message.set(str(error))
    def send(verb,payload="-"):
        if failed[0]:return False
        try:d.request(verb,payload,timeout=2,attempts=1);return True
        except Exception as e:fail(e);return False
    def release(event=None):
        held.clear();pending[:]=[0,0];f8_held[0]=False
        if capture:capture.release()
        send("DBG_QFREE")
        if not failed[0]:message.set("鼠标已释放。点击窗口或按 F8 重新捕获。")
    def take():
        if failed[0]:return
        root.focus_set()
        try:
            if capture.capture():
                heartbeat[0]=mouse_sent[0]=0
                message.set("已捕获鼠标：原始相对移动直传。按 F8 释放，Esc 仍用于游戏菜单。")
        except Exception as e:fail(e)
    def key(event,down):
        if event.keysym=="F8":
            if down and not f8_held[0]:
                if capture.captured:release()
                else:take()
            f8_held[0]=down
            return "break"
        if not capture.captured:return
        k=key_code(event)
        if k is None:return
        if down and k not in held:held.add(k);send("DBG_QKEY",f"{k}:1")
        elif not down and k in held:held.remove(k);send("DBG_QKEY",f"{k}:0")
    def button(event,down):
        if not capture.captured:
            if event.num==1 and down:take()
            return "break"
        k=200 if event.num==1 else 201
        if down and k not in held:held.add(k);send("DBG_QKEY",f"{k}:1")
        elif not down and k in held:held.remove(k);send("DBG_QKEY",f"{k}:0")
    def mouse(dx,dy):
        pending[0]+=dx;pending[1]+=dy
    def tick():
        now=time.monotonic()
        if not failed[0] and capture.captured:
            if now-heartbeat[0]>=.2:
                for k in tuple(held):send("DBG_QKEY",f"{k}:1")
                heartbeat[0]=now
            if pending[0] or pending[1] or now-mouse_sent[0]>=.1:
                dx=max(-1024,min(1024,pending[0]));dy=max(-1024,min(1024,pending[1]))
                pending[0]-=dx;pending[1]-=dy
                send("DBG_QMOUSE",f"{dx}:{dy}");mouse_sent[0]=now
        root.after(16,tick)
    def close():
        release();capture.close();root.destroy()
    try:
        capture=RawMouse(root,mouse,fail)
        root.bind("<KeyPress>",lambda e:key(e,True));root.bind("<KeyRelease>",lambda e:key(e,False))
        for n in (1,3):
            root.bind(f"<ButtonPress-{n}>",lambda e:button(e,True))
            root.bind(f"<ButtonRelease-{n}>",lambda e:button(e,False))
        root.bind("<FocusOut>",release)
        root.protocol("WM_DELETE_WINDOW",close)
        tick();root.mainloop()
    finally:
        if capture:capture.close()
        try:root.destroy()
        except tk.TclError:pass


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
