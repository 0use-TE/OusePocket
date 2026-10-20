"""The production terminal-state boundary must follow catalog publication."""
from pathlib import Path
import os,subprocess
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'.tools/package-completion-check';OUT.mkdir(parents=True,exist_ok=True)
source=(ROOT/'components/ouse_installer/ouse_installer.c').read_text(encoding='utf-8')
function=source[source.index('static void package_finish('):source.index('static void process_package(')]
fixture=r'''
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
typedef int esp_err_t;
static struct {bool waiting,active;void *parsed,*bytes;} package_job;
static unsigned catalog,expected,step,refresh_step,finish_step,publish_step;static int result;static bool was_cancelled;
static int on_ui(unsigned action,void *a,void *b){assert(action==9);return 0;}
static void ouse_bpk_free(void *p){free(p);}
static int refresh_manager(void){refresh_step=++step;catalog=expected;return 0;}
static void ouse_packages_finish(int error,bool cancelled){finish_step=++step;assert(catalog==expected);assert(!package_job.active);result=error;was_cancelled=cancelled;}
static void publish_status(bool busy,int error){publish_step=++step;assert(!busy&&error==result);}
'''
tests=r'''
int main(void){for(unsigned i=0;i<3;i++){step=0;catalog=1;expected=i==0?2:1;package_job.waiting=package_job.active=true;package_job.parsed=malloc(16);package_job.bytes=malloc(32);package_finish(i==1?-1:0,i==2);assert(refresh_step<finish_step&&finish_step<publish_step);assert(result==(i==1?-1:0)&&was_cancelled==(i==2));}puts("PASS production package completion: catalog published before success/failure/cancel terminal state");}
'''
(OUT/'check.c').write_text(fixture+function+tests,encoding='utf-8')
env=dict(os.environ)
for line in subprocess.check_output(['cmd.exe','/d','/c',str(ROOT/'.tools/ouse-core-check/environment.cmd')],encoding='mbcs').splitlines():
    if '=' in line and not line.startswith('='):
        k,v=line.split('=',1);env[k]=v
compiler=Path(env['VCToolsInstallDir'])/'bin/Hostx64/x64/cl.exe'
subprocess.run([str(compiler),'/nologo','/std:c11','/utf-8','/UNDEBUG','/D_CRT_SECURE_NO_WARNINGS',str(OUT/'check.c'),'/Fe'+str(OUT/'check.exe')],cwd=OUT,env=env,check=True)
subprocess.run([str(OUT/'check.exe')],cwd=OUT,check=True,timeout=20)
