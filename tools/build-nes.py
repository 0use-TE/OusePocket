"""Build the standalone Retro-Go NES module with the current P4 toolchain."""
from pathlib import Path
import concurrent.futures
import json
import re
import subprocess
import shutil
ROOT=Path(__file__).resolve().parents[1]
APP=ROOT/"native-apps/nes"
OUT=ROOT/".tools/nes-build"
OUT.mkdir(parents=True,exist_ok=True)
description=json.loads((ROOT/"build/project_description.json").read_text())
cc=Path(description["c_compiler"])
arch=[x for x in (ROOT/"build/toolchain/cflags").read_text().splitlines() if x.startswith(("-march=","-mabi=","-mtune="))]
sysroot=Path(subprocess.check_output([str(cc),"-print-sysroot"],text=True).strip()).resolve()
includes=["-I"+str(APP/"compat"),"-I"+str(APP),"-I"+str(APP/"core"),"-I"+str(ROOT/"components/ouse_native/include"),"-isystem",str(sysroot.parent/"picolibc/include")]
flags=[*arch,*includes,"-include","stdint.h","-std=gnu11","-O2","-g","-fstack-usage","-fPIC","-fvisibility=hidden","-fno-semantic-interposition",
    "-fno-stack-protector","-fno-common","-fdata-sections","-ffunction-sections","-msmall-data-limit=0","-mno-relax",
    "-Dmalloc=ouse_game_malloc","-Dcalloc=ouse_game_calloc","-Drealloc=ouse_game_realloc","-Dfree=ouse_game_free",
    "-Dfopen=ouse_game_fopen","-Dfclose=ouse_game_fclose","-D_Exit=ouse_game_exit","-Dexit=ouse_game_exit","-Dabort=ouse_game_abort","-w"]
sources=sorted((APP/"core").rglob("*.c"))+[APP/"main.c"]

def compile_one(source):
    obj=OUT/(str(source.relative_to(APP)).replace("\\","_").replace("/","_")+".o")
    process=subprocess.run([str(cc),*flags,"-c",str(source),"-o",str(obj)],capture_output=True,text=True)
    if process.returncode:raise RuntimeError(str(source)+"\n"+process.stderr)
    return obj
objects=[]
with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
    for source,future in zip(sources,pool.map(compile_one,sources)):
        objects.append(future)
response=OUT/"link.rsp"
output=ROOT/"dist/native/nes.elf"
output.parent.mkdir(parents=True,exist_ok=True)
response.write_text("\n".join('"'+str(x).replace("\\","/")+'"' for x in objects))
subprocess.run([str(cc),*arch,"-shared","-nostdlib","-nostartfiles","-Wl,-e,app_main","-Wl,--gc-sections",
    "-Wl,-Bsymbolic","-Wl,--undefined=ouse_native_abi","-Wl,-Map="+str(OUT/"nes.map"),"@"+str(response),"-o",str(output)],check=True)
shutil.copyfile(output,OUT/"nes-unstripped.elf")
subprocess.run([str(cc.with_name(cc.name.replace("gcc","strip"))),"--strip-unneeded",str(output)],check=True)
nm=cc.with_name(cc.name.replace("gcc","nm"))
imports=subprocess.check_output([str(nm),"-D","--undefined-only",str(output)],text=True)
(OUT/"imports.txt").write_text(imports)
print(output,output.stat().st_size)
print(imports)
