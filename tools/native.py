"""Build, upload and execute experimental native ESP32-P4 applications."""
from pathlib import Path
import argparse
import json
import subprocess
import time

ROOT=Path(__file__).resolve().parents[1]


def build(source, output, firmware_build):
    description=json.loads((firmware_build/"project_description.json").read_text())
    if description["target"]!="esp32p4":
        raise ValueError("Native applications currently require ESP32-P4")
    compiler=Path(description["c_compiler"])
    flags=(firmware_build/"toolchain/cflags").read_text().splitlines()
    arch=[f.strip() for f in flags if f.startswith(("-mabi=","-march=","-mtune="))]
    if "-mabi=ilp32f" not in arch:
        raise ValueError("The native ABI requires the P4 ilp32f toolchain")
    output.parent.mkdir(parents=True,exist_ok=True)
    subprocess.run([str(compiler),*arch,"-std=c11","-Os","-Wall","-Wextra","-Werror",
        "-fPIC","-fvisibility=hidden","-ffunction-sections","-fdata-sections",
        "-fno-stack-protector","-nostdlib","-nostartfiles","-shared",
        "-Wl,-e,app_main","-Wl,--gc-sections","-Wl,--undefined=ouse_native_abi",
        "-I"+str(ROOT/"components/ouse_native/include"),str(source),"-o",str(output)],check=True)
    strip=compiler.with_name(compiler.name.replace("gcc","strip"))
    subprocess.run([str(strip),"--strip-unneeded","--remove-section=.comment",
        "--remove-section=.riscv.attributes",str(output)],check=True)
    print(f"Built {output} ({output.stat().st_size} bytes)")


def status(device):
    return json.loads(bytes.fromhex(device.request("DBG_ELF")).decode())


def upload(device, local, remote):
    from bpk import crc
    data=local.read_bytes()
    if not 52<=len(data)<=1024*1024:
        raise ValueError("ELF must be between 52 bytes and 1 MiB")
    device.request("DBG_FBEGIN",f"{remote}:{len(data)}:{crc(data):08x}")
    try:
        for offset in range(0,len(data),512):
            device.request("DBG_FDATA",f"{offset:x}:"+data[offset:offset+512].hex())
        device.request("DBG_FEND",timeout=15)
    except BaseException:
        device.request("DBG_FABORT")
        raise
    expected=f"{len(data)}:{crc(data):08x}"
    if device.request("DBG_FINFO",remote)!=expected:
        raise RuntimeError("Uploaded ELF checksum mismatch")


def upload_memory(device, local, install=False):
    from bpk import crc
    data=local.read_bytes()
    if not 52<=len(data)<=1024*1024:
        raise ValueError("ELF must be between 52 bytes and 1 MiB")
    device.request("DBG_ELFBEGIN",f"{len(data)}:{crc(data):08x}")
    try:
        for offset in range(0,len(data),512):
            device.request("DBG_ELFDATA",f"{offset:x}:"+data[offset:offset+512].hex())
        return device.request("DBG_ELFEND","install" if install else "-",timeout=15)
    except BaseException:
        device.request("DBG_ELFABORT")
        raise


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    sub=parser.add_subparsers(dest="command",required=True)
    p=sub.add_parser("build")
    p.add_argument("source",type=Path)
    p.add_argument("output",type=Path)
    p.add_argument("--firmware-build",type=Path,default=ROOT/"build")
    for command in ("run","install","status","stop"):
        p=sub.add_parser(command)
        p.add_argument("--port",default="COM10")
        p.add_argument("--baud",type=int,default=921600)
        p.add_argument("--firmware-build",type=Path,default=ROOT/"build")
        if command in ("run","install"):
            p.add_argument("elf",type=Path)
            p.add_argument("--wait",type=float,default=15,help="Seconds to observe completion")
        if command=="run":
            p.add_argument("--name",default="native-demo.elf")
            p.add_argument("--store",action="store_true",help="Save to SD and run from there; default uploads directly to RAM")
    args=parser.parse_args()
    if args.command=="build":
        build(args.source.resolve(),args.output.resolve(),args.firmware_build.resolve())
        return
    from device import Device
    if args.command in ("run","install") and args.wait<=0:
        parser.error("Use a positive wait duration")
    if args.command=="run" and (not args.name.isascii() or not args.name.endswith(".elf") or
        any(c not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_- ." for c in args.name) or
        " " in args.name or ".." in args.name or len(args.name)>100 or args.wait<=0):
        parser.error("Use a simple ASCII .elf name and a positive wait duration")
    with Device(args.port,elf=args.firmware_build/"ouse_core_desktop.elf",baud=args.baud) as device:
        if "debug=1" not in device.request("HELLO",timeout=30):
            raise RuntimeError("Enable device debugging in Settings first")
        if args.command=="stop":device.request("DBG_ELFSTOP")
        if args.command in ("run","install"):
            if status(device)["active"]:
                raise RuntimeError("Another native application is active")
            if args.command=="install":print(upload_memory(device,args.elf,install=True))
            elif args.store:
                remote="Downloads/"+args.name
                upload(device,args.elf,remote)
                print(device.request("DBG_ELFRUN",remote))
            else:print(upload_memory(device,args.elf))
            deadline=time.monotonic()+args.wait
            while True:
                state=status(device)
                if not state["active"]:break
                if time.monotonic()>=deadline:
                    if args.command=="install":
                        raise TimeoutError("Installation is still pending; query status before retrying")
                    device.request("DBG_ELFSTOP")
                    raise TimeoutError("Stop requested; query status to confirm the module has returned")
                time.sleep(.1)
            print(json.dumps(state,ensure_ascii=False,indent=2))
            expected="installed" if args.command=="install" else "finished"
            if state["phase"]!=expected:raise RuntimeError("Native operation did not finish successfully")
        else:print(json.dumps(status(device),ensure_ascii=False,indent=2))
        print("Evidence:",device.output.resolve())


if __name__=="__main__":main()
