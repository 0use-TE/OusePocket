"""Install and control the Retro-Go NES ELF app on OusePocket."""
import argparse
from pathlib import Path
import time
from bpk import crc
from device import Device,ProtocolError
from native import upload_memory,status
from game_session import stop_previous
ROOT=Path(__file__).resolve().parents[1]
KEYS={'Up':128,'Down':129,'Left':130,'Right':131,'Return':13,'Tab':9,
      'Shift_L':134,'Shift_R':134,'space':32}

def upload_rom(d,path,remote=None):
    data=path.read_bytes()
    if not 16<=len(data)<=2*1024*1024 or data[:4]!=b'NES\x1a':raise ValueError('Use an iNES .nes ROM up to 2 MiB')
    expected=f'{len(data)}:{crc(data):08x}'
    remote=remote or f'Downloads/nes-{crc(data):08x}.nes'
    try:actual=d.request('DBG_FINFO',remote,timeout=60,attempts=1)
    except ProtocolError as e:
        if 'ESP_ERR_NOT_FOUND' not in str(e):raise
        actual=None
    if actual is not None:
        if actual!=expected:raise RuntimeError(f'{remote} already exists with different data')
        return remote
    d.request('DBG_FBEGIN',f'{remote}:{len(data)}:{crc(data):08x}')
    try:
        for offset in range(0,len(data),512):d.request('DBG_FDATA',f'{offset:x}:'+data[offset:offset+512].hex())
        d.request('DBG_FEND',timeout=20)
    except BaseException:
        d.request('DBG_FABORT');raise
    if d.request('DBG_FINFO',remote,timeout=60,attempts=1)!=expected:raise RuntimeError('ROM checksum mismatch')
    return remote

def play(d):
    import tkinter as tk
    root=tk.Tk();root.title('OusePocket NES — 红白机控制器');root.geometry('610x390')
    tk.Label(root,text='方向键 / WASD 移动 · Z / J 为 A · X / K 为 B\nEnter 开始 · Tab / Shift 选择 · R 重启 · Esc 关闭控制器',
             font=('Microsoft YaHei UI',12)).pack(pady=18)
    message=tk.StringVar(value='点此窗口后即可用键盘控制，也可以按下方按钮。')
    held=set();sources={};failed=False
    def send(verb,payload='-'):
        nonlocal failed
        if failed:return
        try:d.request(verb,payload,timeout=2,attempts=1)
        except Exception as e:failed=True;held.clear();sources.clear();message.set(str(e))
    def edge(source,code,down):
        if failed:return
        if down:sources[source]=code
        else:sources.pop(source,None)
        wanted=set(sources.values())
        for k in wanted-held:send('DBG_QKEY',f'{k}:1')
        for k in held-wanted:send('DBG_QKEY',f'{k}:0')
        if failed:return
        held.clear();held.update(wanted)
    def release(event=None):
        held.clear();sources.clear();send('DBG_QFREE')
    def close():
        release();root.after_cancel(ticker);root.destroy()
    def key(e,down):
        if e.keysym=='Escape':
            if down:close()
            return 'break'
        code=KEYS.get(e.keysym)
        if code is None and e.keysym.lower() in ('w','a','s','d','z','x','j','k','r'):code=ord(e.keysym.lower())
        if code is not None:edge('key:'+e.keysym,code,down)
        return 'break'
    pad=tk.Frame(root);pad.pack(pady=10)
    for label,code,row,col in [('↑',128,0,1),('←',130,1,0),('↓',129,1,1),('→',131,1,2),
                                ('B',ord('x'),1,4),('A',ord('z'),0,5),('Select',9,2,1),('Start',13,2,4)]:
        b=tk.Button(pad,text=label,width=7,height=2,takefocus=False);b.grid(row=row,column=col,padx=4,pady=4)
        b.bind('<ButtonPress-1>',lambda e,c=code:edge('button:'+str(c),c,True))
        b.bind('<ButtonRelease-1>',lambda e,c=code:edge('button:'+str(c),c,False))
    tk.Label(root,textvariable=message,wraplength=570).pack(pady=8)
    def tick():
        nonlocal ticker
        for k in tuple(held):send('DBG_QKEY',f'{k}:1')
        ticker=root.after(200,tick)
    root.bind('<KeyPress>',lambda e:key(e,True));root.bind('<KeyRelease>',lambda e:key(e,False))
    root.bind('<FocusOut>',release);root.protocol('WM_DELETE_WINDOW',close)
    ticker=root.after(200,tick)
    try:root.mainloop()
    finally:
        try:root.after_cancel(ticker);root.destroy()
        except tk.TclError:pass

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('command',choices=('install','start','stop','status','play','rom','select'))
    p.add_argument('argument',nargs='?');p.add_argument('--port',default='COM10');a=p.parse_args()
    with Device(a.port) as d:
        print(d.request('HELLO',timeout=30),flush=True)
        if a.command=='install':
            stop_previous(d)
            print(upload_memory(d,Path(a.argument or ROOT/'dist/native/nes.elf'),install='nes'),flush=True)
            end=time.monotonic()+30
            while status(d)['active']:
                if time.monotonic()>end:raise TimeoutError('NES install pending')
                time.sleep(.1)
            if status(d)['phase']!='installed':raise RuntimeError(status(d))
            remote=upload_rom(d,ROOT/'native-apps/nes/demo/2048.nes','Downloads/2048.nes')
            print('Demo ROM:',d.request('DBG_NROM',remote),flush=True)
        elif a.command=='start':
            stop_previous(d);d.request('DBG_OPEN','ouse.nes');end=time.monotonic()+20
            while True:
                s=status(d)
                if not s['active']:raise RuntimeError(s)
                if s['phase']=='running':break
                if time.monotonic()>end:raise TimeoutError('NES startup pending')
                time.sleep(.1)
        elif a.command in ('rom','select'):
            if not a.argument:p.error('rom requires a local .nes; select requires Downloads/name.nes')
            stop_previous(d)
            remote=upload_rom(d,Path(a.argument)) if a.command=='rom' else a.argument
            print('Selected ROM:',d.request('DBG_NROM',remote),flush=True)
        elif a.command=='stop':d.request('DBG_ELFSTOP')
        elif a.command=='play':play(d)
        print(status(d));print(d.request('DBG_QUAKE'));print(d.request('DBG_QAUDIO'));print('Evidence:',d.output.resolve())
if __name__=='__main__':main()
