"""Windows Raw Input mouse capture for the Quake II serial controller."""
import ctypes as C
from ctypes import wintypes as W
import sys

class Device(C.Structure):
    _fields_=[('page',W.USHORT),('usage',W.USHORT),('flags',W.DWORD),('target',W.HWND)]

class Header(C.Structure):
    _fields_=[('kind',W.DWORD),('size',W.DWORD),('device',W.HANDLE),('wparam',W.WPARAM)]

class Mouse(C.Structure):
    _fields_=[('flags',W.USHORT),('buttons',W.ULONG),('raw_buttons',W.ULONG),
              ('dx',W.LONG),('dy',W.LONG),('extra',W.ULONG)]

class Packet(C.Structure):
    _fields_=[('header',Header),('mouse',Mouse)]

class RawMouse:
    def __init__(self,root,on_move,on_error):
        if sys.platform!='win32':raise RuntimeError('Mouse capture requires Windows Raw Input')
        self.root=root;self.on_move=on_move;self.on_error=on_error
        self.captured=False;self.closed=False;self.position=None
        self.api=C.WinDLL('user32',use_last_error=True)
        self.api.RegisterRawInputDevices.argtypes=[C.POINTER(Device),W.UINT,W.UINT]
        self.api.RegisterRawInputDevices.restype=W.BOOL
        self.api.GetRawInputData.argtypes=[W.HANDLE,W.UINT,C.c_void_p,C.POINTER(W.UINT),W.UINT]
        self.api.GetRawInputData.restype=W.UINT
        self.api.ClipCursor.argtypes=[C.POINTER(W.RECT)];self.api.ClipCursor.restype=W.BOOL
        self.api.GetCursorPos.argtypes=[C.POINTER(W.POINT)];self.api.GetCursorPos.restype=W.BOOL
        self.api.SetCursorPos.argtypes=[C.c_int,C.c_int];self.api.SetCursorPos.restype=W.BOOL
        self.api.GetClientRect.argtypes=[W.HWND,C.POINTER(W.RECT)];self.api.GetClientRect.restype=W.BOOL
        self.api.ClientToScreen.argtypes=[W.HWND,C.POINTER(W.POINT)];self.api.ClientToScreen.restype=W.BOOL
        self.api.GetAncestor.argtypes=[W.HWND,W.UINT];self.api.GetAncestor.restype=W.HWND
        self.api.GetForegroundWindow.restype=W.HWND
        self.api.CallWindowProcW.argtypes=[C.c_void_p,W.HWND,W.UINT,W.WPARAM,W.LPARAM]
        self.api.CallWindowProcW.restype=C.c_ssize_t
        self.set_proc=getattr(self.api,'SetWindowLongPtrW',self.api.SetWindowLongW)
        self.set_proc.argtypes=[W.HWND,C.c_int,C.c_void_p];self.set_proc.restype=C.c_void_p
        root.update_idletasks();self.hwnd=root.winfo_id()
        self.callback=C.WINFUNCTYPE(C.c_ssize_t,W.HWND,W.UINT,W.WPARAM,W.LPARAM)(self._window_proc)
        C.set_last_error(0)
        self.previous=self.set_proc(self.hwnd,-4,C.cast(self.callback,C.c_void_p))
        if not self.previous:raise C.WinError(C.get_last_error())
        device=Device(1,2,0,self.hwnd) # Mouse TLC; foreground delivery only.
        if not self.api.RegisterRawInputDevices(C.byref(device),1,C.sizeof(Device)):
            self.set_proc(self.hwnd,-4,self.previous)
            raise C.WinError(C.get_last_error())

    def _window_proc(self,hwnd,message,wparam,lparam):
        if message==0xff and self.captured: # WM_INPUT
            try:self._read(lparam)
            except Exception as error:
                self.release();self.on_error(error)
        # Forward WM_INPUT as well: Windows performs foreground packet cleanup.
        return self.api.CallWindowProcW(self.previous,hwnd,message,wparam,lparam)

    def _read(self,handle):
        size=W.UINT()
        if self.api.GetRawInputData(handle,0x10000003,None,C.byref(size),C.sizeof(Header))==0xffffffff:
            raise C.WinError(C.get_last_error())
        if size.value<C.sizeof(Packet) or size.value>65536:return
        data=C.create_string_buffer(size.value)
        count=self.api.GetRawInputData(handle,0x10000003,data,C.byref(size),C.sizeof(Header))
        if count==0xffffffff:raise C.WinError(C.get_last_error())
        if count<C.sizeof(Packet):return
        packet=Packet.from_buffer_copy(data)
        if packet.header.kind==0 and not (packet.mouse.flags&1):
            # Raw relative counts: no desktop coordinates, acceleration,
            # cursor recentering deltas or controller sensitivity multiplier.
            self.on_move(packet.mouse.dx,packet.mouse.dy)

    def capture(self):
        if self.closed:return False
        if self.api.GetForegroundWindow()!=self.api.GetAncestor(self.hwnd,2):return False
        rect=W.RECT();point=W.POINT();saved=W.POINT()
        if not self.api.GetClientRect(self.hwnd,C.byref(rect)):raise C.WinError(C.get_last_error())
        point.x=(rect.right-rect.left)//2;point.y=(rect.bottom-rect.top)//2
        if not self.api.ClientToScreen(self.hwnd,C.byref(point)):raise C.WinError(C.get_last_error())
        if self.api.GetCursorPos(C.byref(saved)):self.position=(saved.x,saved.y)
        # Pin the hidden pointer to one client pixel. Raw deltas continue even
        # at the clip boundary, allowing unlimited turns without hitting edges.
        clip=W.RECT(point.x,point.y,point.x+1,point.y+1)
        if not self.api.ClipCursor(C.byref(clip)):raise C.WinError(C.get_last_error())
        self.captured=True
        try:self.root.configure(cursor='none')
        except Exception:self.release();raise
        return True

    def release(self):
        if not self.captured:return
        self.captured=False;self.api.ClipCursor(None)
        try:self.root.configure(cursor='')
        except Exception:pass # The native cursor must also unlock after Tk teardown.
        finally:
            if self.position:self.api.SetCursorPos(*self.position);self.position=None

    def close(self):
        if self.closed:return
        self.release();self.closed=True
        device=Device(1,2,1,None) # RIDEV_REMOVE requires a NULL target.
        self.api.RegisterRawInputDevices(C.byref(device),1,C.sizeof(Device))
        self.set_proc(self.hwnd,-4,self.previous)
