"""PC-only failure injection for the real Device UART owner; never opens a COM port."""
from pathlib import Path
import base64
import json
import queue
import sys
import threading
import time
import types
import unittest
from unittest.mock import patch
from uuid import uuid4

import bpk
import device as control


ROOT=Path(__file__).resolve().parents[1]
OUTPUT=ROOT/".tools/device-transport-check"/uuid4().hex


class FakeSerial:
    def __init__(self,plan,**kwargs):
        self.plan=plan
        self.options=kwargs
        self.port=None
        self.is_open=False
        self.open_count=self.close_count=0
        self.writes=[]
        self.control_lines=[]
        self._dtr=self._rts=False
        self.pending=bytearray()
        self.rx=queue.Queue()
        self.contact=None

    @property
    def dtr(self):return self._dtr
    @dtr.setter
    def dtr(self,value):self._dtr=value;self.control_lines.append(("dtr",value))
    @property
    def rts(self):return self._rts
    @rts.setter
    def rts(self,value):self._rts=value;self.control_lines.append(("rts",value))
    @property
    def in_waiting(self):return len(self.pending)

    def open(self):
        self.open_count+=1
        if self.plan.get("open_error"):raise self.plan["open_error"]
        self.is_open=True

    def close(self):
        self.close_count+=1
        self.is_open=False
        self.rx.put(b"")
        if self.plan.get("close_error"):raise self.plan["close_error"]

    def cancel_read(self):
        self.rx.put(b"")
        if self.plan.get("cancel_error"):raise self.plan["cancel_error"]

    def read(self,n):
        if not self.pending:
            try:chunk=self.rx.get(timeout=.01)
            except queue.Empty:return b""
            if isinstance(chunk,Exception):raise chunk
            self.pending.extend(chunk)
        chunk=bytes(self.pending[:n]);del self.pending[:n]
        return chunk

    def write(self,data):
        data=bytes(data)
        self.writes.append(data)
        if data==b"\n":
            if self.plan.get("initial_write_error"):raise self.plan["initial_write_error"]
            return len(data)
        _,token,sequence,verb,payload,checksum=data.decode("ascii").strip().split(" ")
        body=data.strip().rsplit(b" ",1)[0]
        if bpk.crc(body)!=int(checksum,16):raise AssertionError("Bad fixture request CRC")
        if self.plan.get("write_error_verb")==verb:raise self.plan["write_error"]
        handler=self.plan.get("handler")
        reply=handler(self,verb,payload) if handler else self.reply(verb,payload)
        if reply is not None:
            status,result=reply
            self.rx.put(bpk.wire(int(token,16),int(sequence,16),status,result))
        return len(data)

    def reply(self,verb,payload):
        if verb=="HELLO":return "OK","core=fixture,storage=ready,debug=1"
        if verb=="DBG_TOUCH":
            x,y,pressed=map(int,payload.split(":"))
            if x<0 or y<0:return "ERR","ESP_ERR_INVALID_ARG"
            self.contact=(x,y) if pressed else None
            if pressed and self.plan.get("drop_press_ack"):return None
        if verb=="DBG_STATE":return "OK",json.dumps(dict(active="",width=320,height=480)).encode().hex()
        if verb=="DBG_TREE":return "OK",f"4:{bpk.crc(b'test'):08x}"
        if verb=="DBG_READ":return "OK",b"test".hex()
        return "OK","-"

    def flush(self):
        if self.plan.get("flush_error"):raise self.plan["flush_error"]

    def verbs(self):return [data.decode("ascii").split(" ")[3] for data in self.writes if data.startswith(b"OPK1 ")]


class FastClock:
    """Accelerate only Device's bounded observations while its reader stays live."""
    def __init__(self):self.now=0
    def monotonic(self):return self.now
    def time(self):return time.time()
    def sleep(self,seconds):
        time.sleep(.001)
        self.now+=seconds


class TransportTests(unittest.TestCase):
    def setUp(self):
        self.directory=OUTPUT/self._testMethodName
        self.directory.mkdir(parents=True)
        self.elf=self.directory/"fixture.elf"
        self.elf.write_bytes(b"PC transport fixture; not firmware")
        self.plan={}
        self.serials=[]
        def factory(**kwargs):
            serial=FakeSerial(self.plan,**kwargs);self.serials.append(serial);return serial
        self.serial_patch=patch.dict(sys.modules,{"serial":types.SimpleNamespace(Serial=factory)})
        self.serial_patch.start()
        self.addCleanup(self.serial_patch.stop)
        self.output=self.directory/"evidence"

    def make(self):return control.Device("FAKE-NOT-A-COM-PORT",self.output,self.elf)
    def report(self):return json.loads((self.output/"transport.json").read_text(encoding="utf-8"))
    def wait(self,predicate):
        deadline=time.monotonic()+2
        while not predicate() and time.monotonic()<deadline:time.sleep(.002)
        self.assertTrue(predicate(),"Reader fixture did not reach its expected state")
    def assert_closed(self,device):
        self.assertFalse(device.thread and device.thread.is_alive())
        self.assertTrue(device.capture.raw.closed)
        self.assertTrue(device.action_log.closed)
        self.assertTrue(self.report()["closed"])
        self.assertTrue(self.report()["capture_closed"])
        self.assertEqual(self.serials[-1].open_count,1)
        self.assertFalse(any(value for _,value in self.serials[-1].control_lines),"A fixture must never reset the board")

    def test_open_failure_closes_constructor_resources(self):
        self.plan["open_error"]=PermissionError(13,"Access denied",None,5)
        with self.assertRaises(PermissionError):self.make()
        self.assertEqual(self.serials[-1].close_count,1)
        self.assertTrue(json.loads((self.output/"capture.json").read_text())["closed"])
        self.assertTrue(self.report()["failed"])
        self.assertEqual(self.report()["errors"][0]["phase"],"initialize")

    def test_healthy_handshake_resource_touch_and_swipe_close_cleanly(self):
        device=self.make();serial=self.serials[-1]
        with device:
            self.assertIn("debug=1",device.request("HELLO"))
            self.assertEqual(device.resource("DBG_TREE"),b"test")
            self.assertEqual(device.tap(40,50)["width"],320)
            self.assertEqual(device.swipe(40,50,60,70,80)["height"],480)
        self.assertIsNone(serial.contact)
        self.assertFalse(self.report()["failed"])
        self.assertIn("DBG_FREE",serial.verbs())
        self.assertEqual(len(device.actions),len(serial.verbs()))
        self.assert_closed(device)

    def test_initial_write_failure_stops_reader(self):
        self.plan["initial_write_error"]=OSError("initial synchronization failed")
        with self.assertRaisesRegex(OSError,"initial synchronization"):self.make()
        self.assertFalse(self.serials[-1].is_open)
        self.assertFalse(self.report()["reader_alive"])
        self.assertTrue(self.report()["capture_closed"])

    def test_capture_constructor_failure_closes_raw_handle(self):
        instances=[]
        class BrokenCapture(control.Capture):
            def __new__(cls):
                obj=super().__new__(cls);instances.append(obj);return obj
            def save(self):raise OSError("metadata cannot be written")
        with patch.object(control,"Capture",BrokenCapture):
            with self.assertRaisesRegex(OSError,"metadata cannot be written"):self.make()
        self.assertTrue(instances[0].raw.closed)
        self.assertEqual(self.serials,[])
        self.assertEqual(self.report()["errors"][0]["phase"],"initialize")

    def test_existing_evidence_is_never_overwritten(self):
        self.output.mkdir()
        (self.output/"transport.json").write_text("original evidence")
        with self.assertRaises(FileExistsError):self.make()
        self.assertEqual((self.output/"transport.json").read_text(),"original evidence")
        self.assertEqual(self.serials,[])

    def test_reader_winerror_stops_all_new_writes_and_preserves_contact(self):
        device=self.make();serial=self.serials[-1]
        device.request("HELLO");device.touch(30,40,True)
        error=PermissionError(13,"GetOverlappedResult failed",None,5)
        serial.rx.put(error);self.wait(lambda:device.error is not None)
        count=len(serial.writes)
        with self.assertRaisesRegex(RuntimeError,"GetOverlappedResult"):device.request("DBG_STATE")
        self.assertEqual(len(serial.writes),count)
        with self.assertRaisesRegex(RuntimeError,"Device session failed"):device.close()
        self.assertEqual(len(serial.writes),count)
        self.assertIs(device.error,error)
        self.assertEqual(self.report()["contact"],dict(x=30,y=40,acknowledged=True))
        self.assertEqual(self.report()["errors"][0]["phase"],"serial-read")
        self.assert_closed(device)

    def test_write_failure_is_not_retried_or_reconnected(self):
        device=self.make();serial=self.serials[-1]
        self.plan.update(write_error_verb="DBG_STATE",write_error=PermissionError(13,"write denied",None,5))
        with self.assertRaisesRegex(RuntimeError,"write denied"):device.request("DBG_STATE",attempts=3)
        self.assertEqual(serial.verbs(),["DBG_STATE"])
        with self.assertRaises(RuntimeError):device.close()
        self.assertEqual(self.report()["errors"][0]["phase"],"serial-write:DBG_STATE")
        self.assertFalse(self.report()["automatic_reconnect"])
        self.assert_closed(device)

    def test_flush_failure_keeps_the_test_failed_despite_queued_ack(self):
        device=self.make()
        self.plan["flush_error"]=OSError("flush denied")
        with self.assertRaisesRegex(RuntimeError,"flush denied"):device.request("HELLO")
        with self.assertRaises(RuntimeError):device.close()
        self.assertTrue(self.report()["failed"])
        self.assert_closed(device)

    def test_lost_press_ack_is_released_but_original_timeout_is_retained(self):
        device=self.make();serial=self.serials[-1]
        self.plan["drop_press_ack"]=True
        original=device.request
        def quick(verb,payload="-",**kwargs):return original(verb,payload,timeout=.02,attempts=1)
        with patch.object(device,"request",quick):
            with self.assertRaises(TimeoutError):device.touch(11,22,True)
            self.assertEqual(device.contact,(11,22))
            self.assertFalse(device._contact_confirmed)
            with self.assertRaisesRegex(RuntimeError,"Device session failed"):device.close()
        self.assertIsNone(serial.contact)
        self.assertIsNone(device.contact)
        self.assertTrue(self.report()["failed"])
        self.assertEqual(self.report()["errors"][0]["phase"],"response-timeout:DBG_TOUCH")
        self.assertEqual(serial.verbs(),["DBG_TOUCH","DBG_TOUCH"])
        self.assert_closed(device)

    def test_tap_releases_even_when_its_initial_press_ack_was_lost(self):
        device=self.make();serial=self.serials[-1]
        self.plan["drop_press_ack"]=True
        original=device.request
        with patch.object(device,"request",lambda verb,payload="-",**kw:original(verb,payload,timeout=.02,attempts=1)):
            with self.assertRaises(TimeoutError):device.tap(10,20)
        self.assertIsNone(serial.contact)
        with self.assertRaises(RuntimeError):device.close()
        self.assert_closed(device)

    def test_lost_override_ack_restores_mode_without_erasing_timeout(self):
        device=self.make();serial=self.serials[-1]
        def handler(port,verb,payload):
            if verb=="DBG_IDLE" and payload=="0":return None
            return port.reply(verb,payload)
        self.plan["handler"]=handler
        with self.assertRaises(TimeoutError):device.request("DBG_IDLE","0",timeout=.02,attempts=1)
        self.assertTrue(device.busy_idle_override)
        with self.assertRaisesRegex(RuntimeError,"Device session failed"):device.close()
        self.assertFalse(device.busy_idle_override)
        self.assertEqual(serial.verbs(),["DBG_IDLE","DBG_IDLE"])
        self.assertTrue(self.report()["failed"])
        self.assert_closed(device)

    def test_verified_protocol_rejection_is_not_a_transport_failure(self):
        device=self.make()
        with self.assertRaises(control.ProtocolError):device.touch(-1,2,True)
        self.assertIsNone(device.contact)
        self.assertIsNone(device.error)
        device.close();device.close()
        self.assertFalse(self.report()["failed"])
        self.assertEqual(self.serials[-1].close_count,1)
        self.assert_closed(device)

    def test_resource_failure_does_not_send_free_after_reader_error(self):
        device=self.make();serial=self.serials[-1]
        error=PermissionError(13,"resource read denied",None,5)
        def handler(port,verb,payload):
            if verb=="DBG_READ":port.rx.put(error);return None
            return port.reply(verb,payload)
        self.plan["handler"]=handler
        with self.assertRaisesRegex(RuntimeError,"resource read denied"):device.resource("DBG_TREE")
        self.assertEqual(serial.verbs(),["DBG_TREE","DBG_READ"])
        with self.assertRaises(RuntimeError):device.close()
        self.assert_closed(device)

    def test_serial_close_failure_does_not_skip_other_evidence_handles(self):
        device=self.make()
        self.plan["close_error"]=OSError("close denied")
        with self.assertRaisesRegex(RuntimeError,"Device cleanup failed"):device.close()
        self.assertEqual(self.report()["cleanup_errors"][0]["phase"],"serial-close")
        self.assert_closed(device)

    def test_context_preserves_original_failure_when_close_also_fails(self):
        device=self.make()
        self.plan["close_error"]=OSError("secondary close failure")
        with self.assertRaisesRegex(ValueError,"primary test failure"):
            with device:raise ValueError("primary test failure")
        self.assertTrue(self.report()["cleanup_errors"])
        self.assert_closed(device)

    def test_caught_reader_error_cannot_make_context_exit_pass(self):
        device=self.make();self.serials[-1].rx.put(OSError("reader failed"))
        self.wait(lambda:device.error is not None)
        with self.assertRaisesRegex(RuntimeError,"Device session failed"):
            with device:pass
        self.assertTrue(self.report()["failed"])
        self.assert_closed(device)

    def test_incomplete_dump_is_received_before_close_without_reset_or_inputs(self):
        device=self.make();serial=self.serials[-1]
        device.request("HELLO")
        serial.rx.put(b"Guru Meditation Error: fixture only\n================= CORE DUMP START =================\n"+
                      base64.b64encode(b"\x08\x00\x00\x00test")+b"\n")
        self.wait(lambda:device.capture.dump is not None)
        self.assertFalse(device.capture.complete)
        before=len(serial.writes)
        def finish():
            time.sleep(.02)
            serial.rx.put(b"================= CORE DUMP END =================\n")
        producer=threading.Thread(target=finish);producer.start()
        with patch.object(control,"time",FastClock()):device.close()
        producer.join()
        self.assertTrue(device.capture.complete)
        self.assertEqual(len(serial.writes),before)
        self.assertTrue(self.report()["panic_seen"])
        self.assertTrue(json.loads((self.output/"capture.json").read_text())["dumps"][0]["length_matches"])
        self.assert_closed(device)

    def test_write_failure_still_allows_healthy_reader_to_finish_dump(self):
        device=self.make();serial=self.serials[-1]
        self.plan.update(write_error_verb="DBG_STATE",write_error=OSError("TX failed; RX still live"))
        with self.assertRaises(RuntimeError):device.request("DBG_STATE")
        serial.rx.put(b"Guru Meditation Error: fixture only\n================= CORE DUMP START =================\n"+
                      base64.b64encode(b"\x08\x00\x00\x00test")+b"\n")
        self.wait(lambda:device.capture.dump is not None)
        before=len(serial.writes)
        def finish():
            time.sleep(.02)
            serial.rx.put(b"================= CORE DUMP END =================\n")
        producer=threading.Thread(target=finish);producer.start()
        with patch.object(control,"time",FastClock()):
            with self.assertRaisesRegex(RuntimeError,"Device session failed"):device.close()
        producer.join()
        self.assertTrue(device.capture.complete)
        self.assertEqual(len(serial.writes),before)
        self.assertFalse(self.report()["reader_failed"])
        self.assertTrue(self.report()["failed"])
        self.assert_closed(device)


if __name__=="__main__":
    print("PC-only fake UART evidence:",OUTPUT,flush=True)
    unittest.main(verbosity=2)
