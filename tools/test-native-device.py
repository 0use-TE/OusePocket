"""Run the native ELF experiment on a connected board; preserves UART evidence."""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import time
from device import Device, ProtocolError
from native import ROOT, build, status, upload_memory

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument("--port",default="COM10")
args=parser.parse_args()
out=ROOT/".tools/native-device-check"
out.mkdir(parents=True,exist_ok=True)
build(ROOT/"native-apps/hello/main.c",out/"hello.elf",ROOT/"build")
(out/"exit.c").write_text('#include "ouse_native_api.h"\nOUSE_NATIVE_APP();\nint app_main(int argc,char **argv){(void)argc;(void)argv;return 7;}\n')
build(out/"exit.c",out/"exit.elf",ROOT/"build")
bad=bytearray((out/"hello.elf").read_bytes());bad[0]=0
(out/"invalid.elf").write_bytes(bad)
bad=bytearray((out/"hello.elf").read_bytes())
marker=bad.index(struct.pack("<III",0x54414e4f,1,28))
struct.pack_into("<I",bad,marker+4,999)
(out/"wrong-abi.elf").write_bytes(bad)
results=[]
with Device(args.port) as device:
    assert "debug=1" in device.request("HELLO",timeout=30)
    assert device.request("DBG_BUILD")==hashlib.sha256((ROOT/"build/ouse_core_desktop.elf").read_bytes()).hexdigest()
    def wait():
        deadline=time.monotonic()+12
        while time.monotonic()<deadline:
            state=status(device)
            if not state["active"]:
                results.append(state);return state
            time.sleep(.05)
        device.request("DBG_ELFSTOP")
        raise TimeoutError("Module did not return; stop requested, evidence preserved")
    def expect_error(command,payload):
        try:device.request(command,payload)
        except ProtocolError:return
        raise AssertionError("Command unexpectedly accepted: "+command)
    # Successful runs exercise .bss initialization, .data relative relocation,
    # host callbacks and reloading the same file without replacing firmware.
    for _ in range(2):
        upload_memory(device,out/"hello.elf")
        state=wait()
        assert state["phase"]=="finished" and state["reported"] and state["result"]==100 and state["exitCode"]==0,state
    time.sleep(.2)
    heap_before=device.request("DBG_HEAP")
    upload_memory(device,out/"hello.elf")
    deadline=time.monotonic()+3
    while status(device)["phase"]!="running":
        assert time.monotonic()<deadline
        time.sleep(.02)
    expect_error("DBG_ELFRUN","Downloads/native-test.elf")
    expect_error("DBG_ELFBEGIN","1608:00000000")
    device.request("DBG_ELFSTOP")
    state=wait()
    assert state["phase"]=="cancelled" and state["stopRequested"] and state["result"]==-1,state
    expect_error("DBG_ELFRUN","../native-test.elf")
    expect_error("DBG_ELFRUN","Downloads/native-test.bin")
    device.request("DBG_ELFRUN","Downloads/no-such-native-test.elf")
    assert wait()["error"]=="ESP_ERR_NOT_FOUND"
    for name in ("invalid","wrong-abi","exit"):
        upload_memory(device,out/(name+".elf"))
        state=wait()
        assert state["phase"]=="failed",state
        if name=="exit":assert state["exitCode"]==7 and state["error"]=="ESP_FAIL",state
        else:assert state["error"]=="ESP_ERR_INVALID_ARG",state
    # A bad transport CRC must not start any task; incomplete uploads abort.
    prior=status(device)["taskId"]
    device.request("DBG_ELFBEGIN","52:00000000")
    expect_error("DBG_ELFDATA","1:00")
    expect_error("DBG_ELFEND","-")
    assert status(device)["taskId"]==prior
    device.request("DBG_ELFBEGIN","52:00000000")
    device.request("DBG_ELFDATA","0:"+bytes(52).hex())
    expect_error("DBG_ELFEND","-")
    assert status(device)["taskId"]==prior
    device.request("DBG_ELFBEGIN","52:00000000")
    device.request("DBG_ELFABORT")
    expect_error("DBG_ELFEND","-")
    device.request("DBG_ELFBEGIN","4096:00000000")
    time.sleep(31)
    expect_error("DBG_ELFEND","-")
    upload_memory(device,out/"hello.elf")
    state=wait()
    assert state["phase"]=="finished" and state["result"]==100,state
    time.sleep(.3)
    heap_after=device.request("DBG_HEAP")
    integrity=device.request("DBG_CHECK")
    assert "ok=1" in integrity,integrity
    report=dict(results=results,heap_before=heap_before,heap_after=heap_after,integrity=integrity,
        evidence=str(device.output.resolve()))
    (out/"result.json").write_text(json.dumps(report,indent=2))
    print(json.dumps(report,indent=2))
print("PASS native execution, repeated reload, .bss reset, relative/import relocation, cooperative stop, busy admission, invalid file/ABI, missing file, nonzero exit and recovery")
