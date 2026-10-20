"""Compile the production SD image binding against real QuickJS and LVGL types."""
from pathlib import Path
import os,subprocess
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'.tools/sd-images-check';OUT.mkdir(exist_ok=True)
HOST=ROOT/'.tools/ouse-core-check';QJS=ROOT/'managed_components/espressif__quickjs-ng/quickjs-ng'
env=dict(os.environ)
for line in subprocess.check_output(['cmd.exe','/d','/c',str(HOST/'environment.cmd')],encoding='mbcs').splitlines():
 if '=' in line and not line.startswith('='):
  key,value=line.split('=',1);env[key]=value
cc=Path(env['VCToolsInstallDir'])/'bin/Hostx64/x64/cl.exe'
args=[str(cc),'/nologo','/O2','/utf-8','/std:c11','/MD','/D_CRT_SECURE_NO_WARNINGS','/DLV_KCONFIG_IGNORE','/DLV_LVGL_H_INCLUDE_SIMPLE','/DLV_CONF_PATH="'+(HOST/'lv_conf.h').as_posix()+'"']
args+=['/I'+str(p) for p in [QJS,ROOT/'managed_components/lvgl__lvgl',HOST,ROOT/'components/ouse_runtime']]
args += [str(ROOT/'tools/fixtures/sd-images.c'),str(ROOT/'.tools/ouse-runtime-check/quickjs-native.lib'),'Ws2_32.lib','/Fe:'+str(OUT/'check.exe')]
subprocess.run(args,cwd=OUT,env=env,check=True)
subprocess.run([str(OUT/'check.exe')],cwd=OUT,check=True)
