"""Control the actual board and preserve UART/crash evidence in one serial owner."""
from pathlib import Path
import argparse
import json
import queue
import secrets
import sys
import threading
import time
import traceback
from datetime import datetime
from bpk import wire, parse_reply, crc
from trace_device_import import Capture


class ProtocolError(RuntimeError):
    """A verified device ERR response, rather than a failed host transport."""


class Device:
    def __init__(self, port="COM10", output=None, elf=None, baud=921600):
        import hashlib
        import shutil
        import serial
        self.output = Path(output or (Path(".tools/diagnostics") / ("device-"+datetime.now().strftime("%Y%m%d-%H%M%S-%f"))))
        self.port_name=port
        self.capture=self.port=self.action_log=self.thread=None
        self._output_created=False
        self._closed=False
        self._initialized=False
        self._reader_failed=False
        self._started=time.time()
        self._errors=[]
        self._cleanup_errors=[]
        self._evidence_lock=threading.RLock()
        self._request_lock=threading.Lock()
        self._contact_confirmed=False
        self.token=secrets.randbits(32)
        self.sequence=0
        self.lines=queue.Queue()
        self.stop=threading.Event()
        self.error=None
        self.ready=False
        self.rebooted=False
        self.contact=None
        self.busy_idle_override=False
        self.lcd_override=False
        self.restore_flush_mode=None
        self.actions=[]
        try:
            self.output.mkdir(parents=True, exist_ok=False)
            self._output_created=True
            elf = Path(elf or Path(__file__).resolve().parents[1]/"build/ouse_core_desktop.elf")
            sha = hashlib.sha256(elf.read_bytes()).hexdigest()
            for p in (elf, elf.with_suffix(".map"), elf.parent/"config/sdkconfig.h"):
                if p.exists():
                    shutil.copy2(p,self.output/p.name)
            # Keep ownership even if Capture fails after opening its raw file.
            self.capture = Capture.__new__(Capture)
            self.capture.__init__(self.output,sha,lambda s: None if s.startswith("OPK1 ") else print(s,file=sys.stderr,flush=True))
            self.action_log=(self.output/"actions.jsonl").open("a",encoding="utf-8",buffering=1)
            self.port = serial.Serial(port=None,baudrate=int(baud),timeout=.02,write_timeout=5,rtscts=False,dsrdtr=False)
            self.port.dtr=False
            self.port.rts=False
            self.port.port=port
            self.port.open()  # no reset, input flush, or baud change
            self.thread=threading.Thread(target=self._reader,daemon=True)
            self.thread.start()
            if self.port.write(b"\n")!=1:
                raise OSError("Short serial synchronization write")
            if self.error is not None:
                raise RuntimeError(str(self.error))
            self._initialized=True
            self._save_transport()
            if self.error is not None:
                raise RuntimeError(str(self.error))
        except BaseException as error:
            self._record_error(error,"initialize",fatal=True)
            # A failed constructor never reaches the caller's with/finally.
            # Release all acquired handles here, preserving its original error.
            self._close_resources()
            raise

    def _save_transport(self):
        if not self._output_created:
            return
        with self._evidence_lock:
            record=dict(port=self.port_name,started=self._started,initialized=self._initialized,
                        closed=self._closed,automatic_reconnect=False,
                        failed=bool(self.error is not None or self._errors or self._cleanup_errors or
                                    self.rebooted or (self.capture and getattr(self.capture,"panic",False))),
                        errors=self._errors,cleanup_errors=self._cleanup_errors,
                        contact=None if self.contact is None else dict(x=self.contact[0],y=self.contact[1],
                                                                      acknowledged=self._contact_confirmed),
                        reader_alive=bool(self.thread and self.thread.is_alive()),
                        reader_failed=self._reader_failed,
                        panic_seen=bool(self.capture and getattr(self.capture,"panic",False)),unexpected_reboot=self.rebooted,
                        raw_bytes=getattr(self.capture,"bytes",0),
                        capture_closed=bool(self.capture and getattr(self.capture,"closed",False)),updated=time.time())
            path=self.output/"transport.json"
            temp=path.with_suffix(".json.tmp")
            try:
                temp.write_text(json.dumps(record,ensure_ascii=False,indent=2),encoding="utf-8")
                temp.replace(path)
            except Exception as error:
                # Do not replace the first failure when evidence storage itself
                # fails. The caller must still fail, even with a healthy board.
                if self.error is None:
                    self.error=error
                print("Transport evidence could not be saved: "+str(error),file=sys.stderr,flush=True)

    def _record_error(self,error,phase,fatal=False,cleanup=False):
        with self._evidence_lock:
            target=self._cleanup_errors if cleanup else self._errors
            target.append(dict(time=time.time(),phase=phase,type=type(error).__name__,message=str(error),
                               traceback="".join(traceback.format_exception(type(error),error,error.__traceback__))))
            if fatal and self.error is None:
                self.error=error
            self._save_transport()

    def _healthy(self):
        return self.capture is not None and not self.capture.panic and not self.rebooted and self.error is None and not self._closed

    def _can_receive(self):
        # A TX/flush failure stops commands, but a healthy RX owner can still
        # finish an already-started panic dump. A failed reader cannot do so.
        return bool(self.thread and self.thread.is_alive() and not self._reader_failed and not self.stop.is_set())

    def _check_transport(self):
        if self._closed or self.stop.is_set():
            raise RuntimeError("Device session is closed")
        if self.capture.panic or self.rebooted:
            raise RuntimeError("Device panicked; preserving coredump, no further inputs sent")
        if self.error is not None:
            raise RuntimeError(str(self.error)) from self.error

    def _reader(self):
        pending=bytearray()
        phase="serial-read"
        try:
            while not self.stop.is_set():
                phase="serial-read"
                chunk=self.port.read(max(1,min(self.port.in_waiting,16384)))
                if not chunk:
                    continue
                if self.stop.is_set():
                    break
                phase="capture-write"
                self.capture.feed(chunk)
                pending.extend(chunk)
                while b"\n" in pending:
                    line,_,tail=pending.partition(b"\n")
                    pending=bytearray(tail)
                    if self.ready and line.startswith(b"ESP-ROM:"):
                        self.rebooted=True
                        print("Unexpected device reboot; stop inputs and preserve following boot/panic logs",file=sys.stderr,flush=True)
                    if b"OPK1 " in line:
                        self.lines.put(bytes(line))
                if len(pending)>65536:
                    pending.clear()
        except Exception as error:
            if not self.stop.is_set():
                self._reader_failed=True
                self._record_error(error,phase,fatal=True)

    def request(self,verb,payload="-",timeout=5,attempts=2):
        with self._request_lock:
            return self._request(verb,payload,timeout,attempts)

    def _request(self,verb,payload,timeout,attempts):
        if not 1<=len(verb)<=12 or any(c not in "ABCDEFGHIJKLMNOPQRSTUVWXYZ_" for c in verb):
            raise ValueError("Protocol verb must be 1..12 uppercase letters/underscores")
        self._check_transport()
        self.sequence+=1
        seq=self.sequence
        self.actions.append(dict(time=time.time(),sequence=seq,verb=verb,payload=payload))
        try:
            self.action_log.write(json.dumps(self.actions[-1],ensure_ascii=False)+"\n")
        except Exception as error:
            self._record_error(error,"action-log:"+verb,fatal=True)
            raise
        frame=wire(self.token,seq,verb,payload)
        # A lost acknowledgement can still mean the diagnostic override took
        # effect. Retain the cleanup obligation until an acknowledged restore.
        overrides=self.busy_idle_override,self.lcd_override
        if verb=="DBG_IDLE" and payload=="0":self.busy_idle_override=True
        if verb=="DBG_LCD" and payload=="0":self.lcd_override=True
        for _ in range(attempts):
            self._check_transport()
            try:
                if self.port.write(frame)!=len(frame):
                    raise OSError("Short serial write during "+verb)
                self.port.flush()
            except Exception as error:
                self._record_error(error,"serial-write:"+verb,fatal=True)
                raise RuntimeError(str(error)) from error
            deadline=time.monotonic()+timeout
            while time.monotonic()<deadline:
                if self.capture.panic or self.rebooted:
                    raise RuntimeError("Device crashed during "+verb+"; collecting UART coredump")
                if self.error:
                    raise RuntimeError(str(self.error))
                try:
                    line=self.lines.get(timeout=.05)
                except queue.Empty:
                    continue
                reply=parse_reply(line,self.token,seq)
                if reply:
                    status,result=reply
                    if status=="ERR":
                        self.busy_idle_override,self.lcd_override=overrides
                        raise ProtocolError(verb+": "+result)
                    if verb=="HELLO":self.ready=True
                    if verb=="DBG_IDLE":self.busy_idle_override=payload=="0"
                    if verb=="DBG_LCD" and payload in ("0","1"):self.lcd_override=payload=="0"
                    return result
        error=TimeoutError(verb+": device did not reply; raw UART saved at "+str(self.output))
        self._record_error(error,"response-timeout:"+verb)
        raise error

    def state(self):
        return json.loads(bytes.fromhex(self.request("DBG_STATE")).decode("utf-8"))

    def list_packages(self):
        return [item for item in self.request("LIST").split(",") if item]

    def resource(self,verb):
        size,checksum=self.request(verb,timeout=15).split(":")
        size=int(size)
        if not 0<size<=256*1024:
            raise ValueError("Invalid device resource size")
        data=bytearray()
        try:
            while len(data)<size:
                n=min(256,size-len(data))
                chunk=bytes.fromhex(self.request("DBG_READ",f"{len(data)}:{n}"))
                if len(chunk)!=n:
                    raise ValueError("Short resource read")
                data.extend(chunk)
            if crc(data)!=int(checksum,16):
                raise ValueError("Resource checksum mismatch")
        finally:
            if self._healthy():
                failing=sys.exc_info()[0] is not None
                try:
                    self.request("DBG_FREE")
                except Exception as error:
                    self._record_error(error,"resource-release",cleanup=True)
                    if not failing:
                        raise
        return bytes(data)

    def tree(self):
        return json.loads(self.resource("DBG_TREE").decode("utf-8"))

    def screenshot(self,path=None):
        data=self.resource("DBG_SHOT")
        if not data.startswith(b"\xff\xd8") or not data.endswith(b"\xff\xd9"):
            raise ValueError("Invalid JPEG")
        path=Path(path or self.output/"screen.jpg")
        path.parent.mkdir(parents=True,exist_ok=True)
        path.write_bytes(data)
        return path.resolve()

    def touch(self,x,y,pressed):
        self._check_transport()
        previous=self.contact,self._contact_confirmed
        if pressed:
            self.contact=(x,y)
            self._contact_confirmed=False
            if previous[0] is None:
                self._save_transport()
        try:
            result=self.request("DBG_TOUCH",f"{int(x)}:{int(y)}:{int(bool(pressed))}")
        except ProtocolError:
            # A verified ERR means this press was not accepted; a timeout or
            # failed host read leaves it uncertain and must retain the lease.
            self.contact,self._contact_confirmed=previous
            self._save_transport()
            raise
        self.contact=(x,y) if pressed else None
        self._contact_confirmed=bool(pressed)
        if not pressed:
            self._save_transport()
        return result

    def tap(self,x,y):
        try:
            self.touch(x,y,True)
            time.sleep(.05)
        finally:
            self._release_contact("tap-release")
        time.sleep(.12)
        return self.state()

    def swipe(self,x,y,end_x,end_y,duration_ms=220,hold_ms=0):
        if not 80<=duration_ms<=3000 or not 0<=hold_ms<=5000:
            raise ValueError("duration_ms=80..3000; hold_ms=0..5000")
        steps=max(4,min(30,duration_ms//35))
        try:
            self.touch(x,y,True)
            for i in range(1,steps+1):
                started=time.monotonic()
                self.touch(round(x+(end_x-x)*i/steps),round(y+(end_y-y)*i/steps),True)
                time.sleep(max(0,duration_ms/steps/1000-(time.monotonic()-started)))
            if hold_ms:
                time.sleep(hold_ms/1000)
        finally:
            self._release_contact("swipe-release")
        time.sleep(.25)
        return self.state()

    def tests(self):
        result=self.request("DBG_TEST",timeout=45,attempts=1)
        fields=dict(p.split("=",1) for p in result.split(","))
        return {k:int(v) for k,v in fields.items()}

    def stress(self,rounds=10):
        if not 1<=rounds<=200:
            raise ValueError("rounds must be 1..200")
        report=[]
        for i in range(rounds):
            for app in ("ouse.counter","ouse.resources","ouse.api-test","ouse.settings"):
                self.request("DBG_OPEN",app)
                state=self.state()
                assert state["active"]==app,state
                w,h=state["width"],state["height"]
                state=self.swipe(w//2,h-6,w//2,h-100,180)
                assert state["active"]=="" and not state["recents"],state
            self.request("DBG_OPEN","ouse.counter")
            state=self.state()
            w,h=state["width"],state["height"]
            state=self.swipe(w//2,h-6,w//2,h-100,180,220)
            assert state["recents"],state
            self.request("DBG_HOME")
            report.append(dict(round=i+1,state=self.state()))
            (self.output/"stress.json").write_text(json.dumps(report,indent=2),encoding="utf-8")
            print(f"Device stress {i+1}/{rounds} passed",file=sys.stderr,flush=True)
        return dict(rounds=rounds,passed=True,log=str(self.output.resolve()))

    def _release_contact(self,phase):
        if self.contact and self._healthy():
            failing=sys.exc_info()[0] is not None
            try:
                self.touch(*self.contact,False)
            except Exception as error:
                self._record_error(error,phase,cleanup=True)
                if not failing:
                    raise

    def _close_resources(self):
        # Every close is independent: a Windows serial-close error must never
        # skip closure of the UART evidence or command journal.
        self.stop.set()
        if self.thread and self.thread.is_alive():
            try:
                cancel=getattr(self.port,"cancel_read",None)
                if cancel is not None:
                    cancel()
            except Exception as error:
                self._record_error(error,"cancel-read",cleanup=True)
            self.thread.join(timeout=2)
        if self.port is not None:
            try:
                self.port.close()
            except Exception as error:
                self._record_error(error,"serial-close",cleanup=True)
        if self.thread and self.thread.is_alive():
            self.thread.join(timeout=2)
        if self.thread and self.thread.is_alive():
            # Do not close a file underneath its still-live writer. The session
            # remains failed and its open evidence handles are reported.
            self._record_error(RuntimeError("UART reader did not stop; capture remains owned by its writer"),
                               "reader-stop",cleanup=True)
        elif self.capture is not None:
            try:
                self.capture.close()
            except Exception as error:
                self._record_error(error,"capture-close",cleanup=True)
                for name in ("dump","raw"):
                    handle=getattr(self.capture,name,None)
                    if handle is not None:
                        try:
                            handle.close()
                        except Exception as nested:
                            self._record_error(nested,"capture-handle-close:"+name,cleanup=True)
        if self.action_log is not None:
            try:
                self.action_log.close()
            except Exception as error:
                self._record_error(error,"action-log-close",cleanup=True)
        self._closed=True
        self._save_transport()

    def close(self):
        if self._closed:
            # The session never becomes usable again. A caller may retry only
            # handle cleanup after a failed close, without any UART commands.
            if ((self.port is not None and getattr(self.port,"is_open",False)) or
                (self.capture is not None and not getattr(self.capture,"closed",False)) or
                (self.thread is not None and self.thread.is_alive())):
                self._close_resources()
                if self._cleanup_errors:
                    raise RuntimeError("Device cleanup failed; see "+str(self.output/"transport.json"))
            return
        try:
            try:
                self._release_contact("close-touch-release")
            except Exception:
                pass  # recorded above; continue releasing independent resources
            for needed,verb,payload in ((self.lcd_override,"DBG_LCD","1"),
                                        (self.restore_flush_mode is not None,"DBG_FLUSH",self.restore_flush_mode),
                                        (self.busy_idle_override,"DBG_IDLE","1")):
                if needed and self._healthy():
                    try:
                        self.request(verb,payload)
                    except Exception as error:
                        self._record_error(error,"close-restore:"+verb,cleanup=True)
            if self.capture is not None:
                if self.rebooted and not self.capture.panic:
                    deadline=time.monotonic()+15
                    while not self.capture.panic and self._can_receive() and time.monotonic()<deadline:
                        time.sleep(.1)
                if self.capture.panic and not self.capture.complete:
                    deadline=time.monotonic()+180
                    while not self.capture.complete and self._can_receive() and time.monotonic()<deadline:
                        time.sleep(.1)
                if self.capture.complete and self._can_receive():
                    # Keep the same owner through reboot and RTC fault output.
                    deadline=time.monotonic()+10
                    while self._can_receive() and time.monotonic()<deadline:
                        time.sleep(.1)
                if self.capture.dump is not None and self._can_receive():
                    deadline=time.monotonic()+180
                    while not self.capture.complete and self._can_receive() and time.monotonic()<deadline:
                        time.sleep(.1)
        finally:
            self._close_resources()
        if self._cleanup_errors:
            raise RuntimeError("Device cleanup failed; see "+str(self.output/"transport.json"))
        if self._errors or self.error is not None:
            raise RuntimeError("Device session failed; see "+str(self.output/"transport.json"))

    def __enter__(self): return self
    def __exit__(self,exc_type,exc_value,traceback):
        try:
            self.close()
        except Exception:
            if exc_type is None:
                raise
        if exc_type is None:
            if self.capture.panic or self.rebooted:
                raise RuntimeError("Device crashed during final observation; evidence: "+str(self.output))
            if self.error:
                raise RuntimeError("Serial capture failed: "+str(self.error))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port",default="COM10")
    parser.add_argument("--baud",type=int,default=921600)
    parser.add_argument("--output")
    parser.add_argument("--elf")
    sub=parser.add_subparsers(dest="command",required=True)
    for name in ("state","tree","tests","home","sync"):
        sub.add_parser(name)
    p=sub.add_parser("screenshot");p.add_argument("path",nargs="?")
    p=sub.add_parser("open");p.add_argument("id")
    p=sub.add_parser("tap");p.add_argument("x",type=int);p.add_argument("y",type=int)
    p=sub.add_parser("swipe")
    for name in ("x","y","end_x","end_y"):p.add_argument(name,type=int)
    p.add_argument("--duration",type=int,default=220);p.add_argument("--hold",type=int,default=0)
    p=sub.add_parser("stress");p.add_argument("--rounds",type=int,default=10)
    args=parser.parse_args()
    with Device(args.port,args.output,args.elf,baud=args.baud) as device:
        if "debug=1" not in device.request("HELLO"):
            raise RuntimeError("Device firmware does not provide debug controls")
        if args.command=="sync":
            packages=device.list_packages()
            state=device.state()
            alive=state.get("alive")
            if not isinstance(alive,list):
                alive=[state["active"]] if state.get("active") else []
            result=dict(installed=packages,active=state.get("active") or "",alive=alive,live=state.get("live",0))
        elif args.command in ("state","tree","tests"):
            result=getattr(device,args.command)()
        elif args.command=="screenshot":result=str(device.screenshot(args.path))
        elif args.command=="home":device.request("DBG_HOME");result=device.state()
        elif args.command=="open":device.request("DBG_OPEN",args.id);result=device.state()
        elif args.command=="tap":result=device.tap(args.x,args.y)
        elif args.command=="swipe":result=device.swipe(args.x,args.y,args.end_x,args.end_y,args.duration,args.hold)
        else:result=device.stress(args.rounds)
        print(json.dumps(result,ensure_ascii=False,indent=2 if args.command!="sync" else None))
        print("Evidence:",device.output.resolve(),file=sys.stderr)
        if args.command=="tests" and result["failures"]:
            raise SystemExit(1)


if __name__=="__main__": main()
