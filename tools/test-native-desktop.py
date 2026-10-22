"""Verify desktop touch launch, lifecycle and persistent native installation on hardware."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import sys
import time
from device import Device
from native import ROOT, build, status, upload_memory

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument("--port",default="COM10")
parser.add_argument("--skip-reboot",action="store_true",help="Verify persistence across app reopen without resetting hardware")
args=parser.parse_args()
out=ROOT/".tools/native-desktop-check"
out.mkdir(parents=True,exist_ok=True)
build(ROOT/"native-apps/hello/main.c",out/"hello.elf",ROOT/"build")
# A distinct result proves that the installed file, rather than the bundled
# fallback, survives reboot. Restore the normal demo at the end of the test.
source=(ROOT/"native-apps/hello/main.c").read_text().replace("host->report(counter);\n    host->log", "host->report(321);\n    host->log")
(out/"persist.c").write_text(source)
build(out/"persist.c",out/"persist.elf",ROOT/"build")
bad=bytearray((out/"hello.elf").read_bytes());bad[0]=0
(out/"invalid.elf").write_bytes(bad)
report={}

def connect():
    d=Device(args.port)
    assert "debug=1" in d.request("HELLO",timeout=30)
    assert d.request("DBG_BUILD")==hashlib.sha256((ROOT/"build/ouse_core_desktop.elf").read_bytes()).hexdigest()
    return d

def wait(d):
    end=time.monotonic()+12
    while time.monotonic()<end:
        s=status(d)
        if not s["active"]:return s
        time.sleep(.04)
    raise TimeoutError("Native operation did not finish")

def click_text(d,caption):
    state=d.state()
    matches=[n for n in d.tree()["nodes"] if n.get("text")==caption and
        0<=n["bounds"][0]<state["width"] and 0<=n["bounds"][1]<state["height"]]
    assert matches,(caption,d.tree())
    x1,y1,x2,y2=matches[0]["bounds"]
    return d.tap((x1+x2)//2,(y1+y2)//2)

def desktop_launch(d):
    d.request("DBG_CLOSE","ouse.native")
    d.request("DBG_HOME")
    for _ in range(d.state()["pages"]+1):
        labels=[n for n in d.tree()["nodes"] if n.get("text")=="原生程序" and
            0<=n["bounds"][0]<d.state()["width"]]
        if labels:break
        s=d.state();d.swipe(s["width"]-35,s["height"]//2,35,s["height"]//2,260)
    else:raise AssertionError("Native program icon missing from launcher")
    d.screenshot(out/"desktop.jpg")
    click_text(d,"原生程序")
    deadline=time.monotonic()+3
    while d.state()["active"]!="ouse.native":
        assert time.monotonic()<deadline
        time.sleep(.05)

with connect() as d:
    desktop_launch(d)
    s=wait(d)
    assert s["phase"]=="finished" and s["result"]==100,s
    time.sleep(.15)
    assert any(n.get("text")=="100" for n in d.tree()["nodes"])
    report["desktop_touch"]=s
    click_text(d,"运行");time.sleep(.15);click_text(d,"停止")
    assert wait(d)["phase"]=="cancelled"
    click_text(d,"运行");time.sleep(.15);d.request("DBG_HOME")
    assert wait(d)["phase"]=="cancelled"
    # The app must not stop an unrelated native session it failed to launch.
    d.request("DBG_CLOSE","ouse.native")
    upload_memory(d,out/"hello.elf")
    external=status(d)["taskId"]
    d.request("DBG_OPEN","ouse.native");d.request("DBG_HOME")
    assert status(d)["active"] and status(d)["taskId"]==external
    d.request("DBG_ELFSTOP");wait(d)
    upload_memory(d,out/"persist.elf",install=True)
    assert wait(d)["phase"]=="installed"
    # Rejected updates must leave the previous durable program untouched.
    upload_memory(d,out/"invalid.elf",install=True)
    assert wait(d)["phase"]=="failed"
    report["before_reboot"]=str(d.output.resolve())

# Release the sole UART owner before the explicitly requested restart test.
if not args.skip_reboot:
    subprocess.run([sys.executable,"-m","esptool","--chip","esp32p4","--port",args.port,
        "--before","default-reset","--after","hard-reset","chip-id"],check=True)

with connect() as d:
    desktop_launch(d)
    s=wait(d)
    assert s["phase"]=="finished" and s["result"]==321,s
    report["persistent_after_reopen" if args.skip_reboot else "persistent_after_reboot"]=s
    upload_memory(d,out/"hello.elf",install=True)
    assert wait(d)["phase"]=="installed"
    d.request("DBG_CLOSE","ouse.native")
    d.request("DBG_OPEN","ouse.native")
    s=wait(d)
    assert s["phase"]=="finished" and s["result"]==100,s
    time.sleep(.2)
    d.screenshot(out/"finished.jpg")
    report["restored_demo"]=s
    report["integrity"]=d.request("DBG_CHECK")
    assert "ok=1" in report["integrity"]
    report["after_reboot"]=str(d.output.resolve())
    (out/"result.json").write_text(json.dumps(report,ensure_ascii=False,indent=2))
    print(json.dumps(report,ensure_ascii=False,indent=2))
print("PASS launcher touch, result UI, Run/Stop, home cancellation, session ownership, durable install, rejected update preservation, "+("reopen" if args.skip_reboot else "reboot")+" and recovery")
