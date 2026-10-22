"""Exercise the production ELF preflight against real compiler output and corruptions."""
from pathlib import Path
import ctypes
import json
import os
import random
import struct
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/".tools/native-elf-check"
OUT.mkdir(parents=True,exist_ok=True)
sys.path.insert(0,str(ROOT/"tools"))
from native import build

build(ROOT/"native-apps/hello/main.c",OUT/"hello.elf",ROOT/"build")
data=(OUT/"hello.elf").read_bytes()
env=dict(os.environ)
vswhere=Path(os.environ.get("ProgramFiles(x86)","C:/Program Files (x86)"))/"Microsoft Visual Studio/Installer/vswhere.exe"
installation=subprocess.check_output([str(vswhere),"-latest","-property","installationPath"],text=True).strip()
batch=OUT/"env.cmd"
batch.write_text(f'@echo off\ncall "{installation}/VC/Auxiliary/Build/vcvars64.bat" >nul\nset\n')
for line in subprocess.check_output(["cmd.exe","/d","/c",str(batch)],encoding="mbcs").splitlines():
    if "=" in line and not line.startswith("="):
        key,value=line.split("=",1);env[key]=value
compiler=Path(env["VCToolsInstallDir"])/"bin/Hostx64/x64/cl.exe"
subprocess.run([str(compiler),"/nologo","/std:c11","/W4","/WX","/LD",
    "/I"+str(ROOT/"components/ouse_native/include"),
    str(ROOT/"components/ouse_native/ouse_native_validate.c"),
    "/link","/EXPORT:ouse_native_validate","/OUT:"+str(OUT/"validate.dll")],cwd=OUT,env=env,check=True)
library=ctypes.CDLL(str(OUT/"validate.dll"))
validate=library.ouse_native_validate
validate.argtypes=[ctypes.c_void_p,ctypes.c_size_t]
validate.restype=ctypes.c_int
def accepted(blob):
    buffer=ctypes.create_string_buffer(bytes(blob))
    return bool(validate(buffer,len(blob)))
assert accepted(data),"The real demo must be accepted"
cases=0
def reject(blob):
    global cases
    assert not accepted(blob),f"Corrupt fixture {cases} accepted"
    cases+=1
def changed(offset,fmt,value):
    blob=bytearray(data);struct.pack_into(fmt,blob,offset,value);return blob
for length in (0,1,16,51,len(data)-1):reject(data[:length])
for offset,fmt,value in ((0,"<I",0),(4,"B",2),(5,"B",2),(16,"<H",2),(18,"<H",62),
    (20,"<I",2),(24,"<I",0xfffffff0),(28,"<I",0xfffffff0),(32,"<I",0xfffffff0),
    (36,"<I",5),(40,"<H",0),(42,"<H",0),(44,"<H",33),(46,"<H",0),
    (48,"<H",129),(50,"<H",128)):
    reject(changed(offset,fmt,value))
phoff=struct.unpack_from("<I",data,28)[0]
phnum=struct.unpack_from("<H",data,44)[0]
loads=[phoff+i*32 for i in range(phnum) if struct.unpack_from("<I",data,phoff+i*32)[0]==1]
for offset,fmt,value in ((loads[0]+4,"<I",0xfffffff0),(loads[0]+20,"<I",0xffffffff),
    (loads[1]+8,"<I",0),(loads[1]+20,"<I",8*1024*1024),
    (loads[0],"<I",3)):
    reject(changed(offset,fmt,value))
shoff=struct.unpack_from("<I",data,32)[0]
shnum=struct.unpack_from("<H",data,48)[0]
sections=[struct.unpack_from("<10I",data,shoff+i*40) for i in range(shnum)]
names=sections[struct.unpack_from("<H",data,50)[0]]
for i,s in enumerate(sections):
    if data[names[4]+s[0]:].split(b"\0",1)[0]==b".ouse_abi":
        for offset,value in ((s[4],0),(s[4]+4,999),(s[4]+8,999)):
            reject(changed(offset,"<I",value))
    if s[1]==4:
        for offset,value in ((shoff+i*40+24,999),(s[4],0xffffffff),(s[4]+4,0xffffff03),
                             (s[4]+4,2),(shoff+i*40+20,s[5]-1)):
            reject(changed(offset,"<I",value))
        syms=sections[s[6]]
        reject(changed(shoff+s[6]*40+4,"<I",8)) # NOBITS must never serve as an input table
        reject(changed(shoff+s[6]*40+24,"<I",999))
reject(data+b"\0"*(1024*1024))
# Bounded random mutations stress table offsets and lengths in the same C validator.
rng=random.Random(428)
for _ in range(10000):
    blob=bytearray(data)
    offset=rng.randrange(len(blob))
    blob[offset]^=rng.randrange(1,256)
    accepted(blob)
print(f"PASS real P4 ELF, {cases} invalid files rejected, 10000 bounded mutations without a crash")
(OUT/"result.json").write_text(json.dumps(dict(valid=True,rejected=cases,mutations=10000),indent=2))
