"""Run the production launcher file persistence function against a disposable filesystem."""
from pathlib import Path
import os
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
output=root/'.tools/launcher-storage-check'
output.mkdir(parents=True,exist_ok=True)
source=(root/'components/ouse_storage/ouse_storage.c').read_text(encoding='utf-8')
function=source[source.index('static esp_err_t launcher_value('):source.index('static esp_err_t execute(')]
fixture=r'''
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <sys/stat.h>
#include <direct.h>
#include <io.h>
#include <windows.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_INVALID_RESPONSE 0x108
#define OUSE_STORAGE_BASE "store"
#define S_ISREG(mode) (((mode)&_S_IFREG)!=0)
#define fsync _commit
#define unlink _unlink
enum {JOB_LAUNCHER_GET,JOB_LAUNCHER_SET,JOB_DEFAULTS_GET,JOB_DEFAULTS_SET};
typedef struct {int operation;char *value;size_t value_size;} storage_job_t;
static bool fail_memory,fail_rename;
static void *allocate(size_t size){return fail_memory ? NULL : malloc(size);}
static char *json_copy(const char *value){char *out=allocate(strlen(value)+1);if(out)strcpy(out,value);return out;}
static int replace_file(const char *from,const char *to){return !fail_rename && MoveFileExA(from,to,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH) ? 0 : -1;}
#define rename replace_file
'''
tests=r'''
int main(void){
    assert(!_mkdir("store"));char *result=NULL;storage_job_t get={.operation=JOB_LAUNCHER_GET};
    assert(launcher_value(&get,&result)==ESP_OK && result && !*result);free(result);result=NULL;
    const char *first="OUSE1\nouse.settings\nuser.music\n",*second="OUSE1\nuser.music\nouse.settings\n";
    storage_job_t put={.operation=JOB_LAUNCHER_SET,.value=(char *)first,.value_size=strlen(first)};
    assert(launcher_value(&put,&result)==ESP_OK && !result);
    assert(launcher_value(&get,&result)==ESP_OK && !strcmp(result,first));free(result);result=NULL;
    put.value=(char *)second;put.value_size=strlen(second);fail_rename=true;
    assert(launcher_value(&put,&result)!=ESP_OK);
    assert(launcher_value(&get,&result)==ESP_OK && !strcmp(result,first));free(result);result=NULL;
    assert(_access("store/launcher-order.tmp",0)!=0);fail_rename=false;
    assert(launcher_value(&put,&result)==ESP_OK);
    storage_job_t defaults={.operation=JOB_DEFAULTS_SET,.value="OUSEF1\nuser.gallery\nuser.music\nuser.gallery\n",.value_size=45};
    defaults.value_size=strlen(defaults.value);assert(launcher_value(&defaults,&result)==ESP_OK);
    defaults.operation=JOB_DEFAULTS_GET;assert(launcher_value(&defaults,&result)==ESP_OK && !strcmp(result,defaults.value));free(result);result=NULL;
    assert(launcher_value(&get,&result)==ESP_OK && !strcmp(result,second));free(result);result=NULL;
    fail_memory=true;assert(launcher_value(&get,&result)==ESP_ERR_NO_MEM && !result);fail_memory=false;
    FILE *file=fopen("store/launcher-order.v1","wb");assert(file);const char bad[]={'O',0,'K'};assert(fwrite(bad,1,sizeof(bad),file)==sizeof(bad));fclose(file);
    assert(launcher_value(&get,&result)==ESP_ERR_INVALID_RESPONSE && !result);
    assert(launcher_value(&put,&result)==ESP_OK);
    file=fopen("store/launcher-order.v1","wb");assert(file);assert(!fseek(file,1024*1024,SEEK_SET));fputc('x',file);fclose(file);
    assert(launcher_value(&get,&result)==ESP_ERR_INVALID_SIZE && !result);
    puts("PASS actual launcher persistence: absent file, read/write/replace, atomic rename failure retains previous order, temporary cleanup, low-memory refusal, embedded NUL and oversized-file rejection");
    return 0;
}
'''
(output/'check.c').write_text(fixture+function+tests,encoding='utf-8')
setup=root/'.tools/ouse-system-check/environment.cmd'
assert setup.exists(),'Run tools/test-ouse-system.py first'
environment=dict(os.environ)
for line in subprocess.check_output(['cmd.exe','/d','/c',str(setup)],encoding='mbcs').splitlines():
    if '=' in line and not line.startswith('='):
        key,value=line.split('=',1)
        environment[key]=value
compiler=Path(environment['VCToolsInstallDir'])/'bin/Hostx64/x64/cl.exe'
subprocess.run([str(compiler),'/nologo','/utf-8','/std:c11','/UNDEBUG','/D_CRT_SECURE_NO_WARNINGS',str(output/'check.c'),'/Fe'+str(output/'check.exe')],cwd=output,env=environment,check=True)
with tempfile.TemporaryDirectory(prefix='sandbox-',dir=output) as sandbox:
    subprocess.run([str(output/'check.exe')],cwd=sandbox,check=True)
