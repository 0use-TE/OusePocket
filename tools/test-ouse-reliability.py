"""Focused real coordinator/registry/journal checks with deterministic native doubles."""
from pathlib import Path
import os, subprocess
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'.tools/ouse-reliability-check'
OUT.mkdir(parents=True,exist_ok=True)
BASE=ROOT/'.tools/ouse-services-check'
env=dict(os.environ)
for line in subprocess.check_output(['cmd.exe','/d','/c',str(ROOT/'.tools/ouse-kernel-check/environment.cmd')],encoding='mbcs').splitlines():
    if '=' in line and not line.startswith('='):
        key,value=line.split('=',1);env[key]=value
compiler=Path(env['VCToolsInstallDir'])/'bin/Hostx64/x64/cl.exe'
headers={
 'ouse_core.h': '''#pragma once
#include "ouse_system.h"
#define OUSE_APP_CAPACITY 8
typedef struct {char id[48];} ouse_app_info_t;
esp_err_t ouse_app_get_info(unsigned,ouse_app_info_t*);
esp_err_t ouse_app_close_id(const char*);
esp_err_t ouse_core_post_ui(void (*fn)(void*),void *context);
''',
 'ouse_runtime.h': '''#pragma once
#include "ouse_tasks.h"
typedef struct {unsigned contexts;} ouse_runtime_stats_t;
typedef struct {char app_id[48],background_reason[64];unsigned instance_id;bool background_active;} ouse_runtime_app_stats_t;
typedef struct {unsigned app_count;ouse_runtime_app_stats_t apps[12];} ouse_runtime_diagnostics_t;
void ouse_runtime_get_stats(ouse_runtime_stats_t*);
void ouse_runtime_get_diagnostics(ouse_runtime_diagnostics_t*);
esp_err_t ouse_runtime_end_background(uint32_t);
''',
 'esp_system.h':'#pragma once\nvoid esp_restart(void);\n',
 'unistd.h':'#pragma once\n#include <io.h>\n#define fsync _commit\n#define fileno _fileno\n#define unlink _unlink\n',
}
for name,value in headers.items():(OUT/name).write_text(value,encoding='utf-8')
includes=[OUT]+[ROOT/f'components/{p}/include' for p in ['ouse_system','ouse_tasks','ouse_devices','ouse_storage','ouse_installer','ouse_net','ouse_media','ouse_alarm']]+[ROOT/'components/ouse_installer',BASE]
command=[str(compiler),'/nologo','/std:c11','/experimental:c11atomics','/O2','/MD','/utf-8','/D_CRT_SECURE_NO_WARNINGS']+['/I'+str(p) for p in includes]
for name in ['coordinator','journal']:
    fixture=(ROOT/f'tools/checks/reliability-{name}.c').read_text(encoding='utf-8')
    fixture=fixture.replace('COORDINATOR_SOURCE',(ROOT/'main/ouse_reliability.c').as_posix())
    fixture=fixture.replace('JOURNAL_SOURCE',(ROOT/'components/ouse_installer/ouse_uninstall_journal.c').as_posix())
    source=(ROOT/'components/ouse_installer/ouse_installer.c').read_text(encoding='utf-8')
    fixture=fixture.replace('/* UNINSTALL_FINISH */',source[source.index('static esp_err_t finish_uninstall('):source.index('static esp_err_t install_upload(')])
    path=OUT/f'{name}.c';path.write_text(fixture,encoding='utf-8')
    sources=[path]
    if name=='coordinator':sources += [ROOT/'tools/checks/services-platform.c',ROOT/'components/ouse_tasks/ouse_tasks.c',ROOT/'components/ouse_system/ouse_health.c']
    else:sources += [ROOT/'components/ouse_installer/ouse_protocol.c']
    result=subprocess.run(command+list(map(str,sources))+['/Fe'+str(OUT/f'{name}.exe')],cwd=OUT,env=env,capture_output=True,text=True,errors='replace')
    assert result.returncode==0,(name,result.stdout,result.stderr)
    for case in (['drain','queued-commit','timeout','ui-late','commit-failure','uninstall-pending','pending-launch','owner','admission'] if name=='coordinator' else ['journal']):
        subprocess.run([str(OUT/f'{name}.exe'),case],cwd=OUT,check=True,timeout=15)
