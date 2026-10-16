"""Board-only regression run: Unity, genuine touch events, JPEG, home/overview stress."""
from pathlib import Path
import argparse
import json
import time
from device import Device


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port",default="COM10")
    parser.add_argument("--rounds",type=int,default=10)
    parser.add_argument("--output")
    parser.add_argument("--elf")
    parser.add_argument("--settle",type=float,default=0,help="Seconds to observe boot before beginning the experiment")
    mode=parser.add_mutually_exclusive_group()
    mode.add_argument("--busy-idle",action="store_true",help="Diagnostic A/B: bypass CPU WFI without disabling services")
    mode.add_argument("--no-lcd",action="store_true",help="Diagnostic A/B: render normally but skip LCD SPI transfers")
    mode.add_argument("--yield-flush",action="store_true",help="Diagnostic A/B: yield while waiting for real LCD completion")
    mode.add_argument("--spin-flush",action="store_true",help="Diagnostic A/B: restore LVGL's original busy wait")
    args=parser.parse_args()
    if not 0<=args.settle<=60:parser.error("--settle must be 0..60 seconds")
    with Device(args.port,args.output,args.elf) as device:
        hello=device.request("HELLO",timeout=8)
        assert "debug=1" in hello,hello
        if args.settle:time.sleep(args.settle)
        if args.busy_idle:
            print("Diagnostic idle mode:",device.request("DBG_IDLE","0"),flush=True)
        if args.no_lcd:
            print("Diagnostic LCD transport:",device.request("DBG_LCD","0",timeout=10),flush=True)
        if args.yield_flush or args.spin_flush:
            original=dict(p.split("=",1) for p in device.request("DBG_FLUSH","status").split(","))
            device.restore_flush_mode=original["enabled"]
            print("Diagnostic flush wait:",device.request("DBG_FLUSH","1" if args.yield_flush else "0"),flush=True)
        initial=device.state()
        print("Actual device:",initial,flush=True)
        unity=device.tests()
        assert unity["tests"]==5 and unity["failures"]==0,unity
        print("On-device Unity:",unity,flush=True)
        device.request("DBG_OPEN","ouse.counter")
        time.sleep(.15)
        tree=device.tree()
        before=next(int(n["text"]) for n in tree["nodes"] if n["text"].isdecimal())
        increment=next(n for n in tree["nodes"] if n["text"]=="+1")
        x1,y1,x2,y2=increment["bounds"]
        device.tap((x1+x2)//2,(y1+y2)//2)
        tree=device.tree()
        assert any(n["text"]==str(before+1) for n in tree["nodes"]),tree
        counter=str(device.screenshot(device.output/"counter.jpg"))
        print("Real touch increment + JPEG:",counter,flush=True)
        state=device.state()
        w,h=state["width"],state["height"]
        device.touch(w//2,h-6,True)
        time.sleep(.04)
        device.touch(w//2,h-100,True)
        time.sleep(.24)
        held=device.state()
        assert held["recents"] and held["tracking"],held
        # Verify overview before releasing; release must not click a card.
        overview=str(device.screenshot(device.output/"overview-held.jpg"))
        device.touch(w//2,h-100,False)
        assert device.state()["recents"]
        print("Overview visible while held:",overview,flush=True)
        device.request("DBG_HOME")
        stress=device.stress(args.rounds)
        desktop=str(device.screenshot(device.output/"desktop.jpg"))
        result=dict(core=initial["core"],unity=unity,touch_increment=True,overview_before_release=True,
                    stress=stress,images=[counter,overview,desktop],final=device.state())
        if args.no_lcd:
            status=device.request("DBG_LCD","status")
            fields=dict(p.split("=",1) for p in status.split(","))
            assert fields["enabled"]=="0" and int(fields["skipped"])>0,status
            result["lcd_transport"]=fields
        if args.yield_flush or args.spin_flush:
            fields=dict(p.split("=",1) for p in device.request("DBG_FLUSH","status").split(","))
            lcd=dict(p.split("=",1) for p in device.request("DBG_LCD","status").split(","))
            assert fields["enabled"]==("1" if args.yield_flush else "0"),fields
            if args.yield_flush:assert int(fields["waits"])>0,fields
            assert lcd["enabled"]=="1",lcd
            result["flush_wait"]=fields
            result["lcd_transport"]=lcd
        (device.output/"result.json").write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding="utf-8")
        print(json.dumps(result,ensure_ascii=False,indent=2),flush=True)
        if args.busy_idle:device.request("DBG_IDLE","1")
        if args.no_lcd:device.request("DBG_LCD","1",timeout=10)


if __name__=="__main__":main()
