"""Production service JS bindings on pinned genuine QuickJS, no device/network."""
from pathlib import Path
import os
import subprocess
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'.tools/ouse-services-check'
QJS=ROOT/'managed_components/espressif__quickjs-ng/quickjs-ng'
LIB=ROOT/'.tools/ouse-runtime-check/quickjs-native.lib'
env=dict(os.environ)
for line in subprocess.check_output(['cmd.exe','/d','/c',str(ROOT/'.tools/ouse-kernel-check/environment.cmd')],encoding='mbcs').splitlines():
    if '=' in line and not line.startswith('='):
        key,value=line.split('=',1);env[key]=value
compiler=Path(env['VCToolsInstallDir'])/'bin/Hostx64/x64/cl.exe'
runtime=(ROOT/'components/ouse_runtime/ouse_runtime.c').read_text(encoding='utf-8')
copy=runtime[runtime.index('static bool copy_string('):runtime.index('static JSValue json_property(')]
errors=runtime[runtime.index('static JSValue storage_error('):runtime.index('static bool storage_key(')]
fixture=(ROOT/'tools/checks/services-js.c').read_text(encoding='utf-8').replace('/* COPY_STRING */',copy).replace('/* ERROR_HELPERS */',errors)
fixture=fixture.replace('SERVICES_SOURCE',(ROOT/'components/ouse_runtime/ouse_runtime_services.inc').as_posix())
path=OUT/'services-js.c';path.write_text(fixture,encoding='utf-8')
includes=[OUT,QJS,ROOT/'components/ouse_tasks/include',ROOT/'components/ouse_net/include',ROOT/'components/ouse_media/include',ROOT/'components/ouse_alarm/include',ROOT/'components/ouse_system/include']
command=[str(compiler),'/nologo','/std:c11','/O2','/MD','/utf-8','/D_CRT_SECURE_NO_WARNINGS','/D_GNU_SOURCE','/DQUICKJS_NG_BUILD']
command+=['/I'+str(p) for p in includes]
result=subprocess.run(command+[str(path),str(ROOT/'tools/checks/services-platform.c'),str(ROOT/'components/ouse_tasks/ouse_tasks.c'),str(LIB),'/Fe'+str(OUT/'services-js.exe')],cwd=OUT,env=env,capture_output=True,text=True,errors='replace')
assert result.returncode==0,(result.stdout,result.stderr)
subprocess.run([str(OUT/'services-js.exe')],cwd=OUT,check=True,timeout=20)
