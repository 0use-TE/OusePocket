"""Exercise production LIST/LIST_PAGE framing with a deliberately tiny reply buffer."""
from pathlib import Path
import os
import subprocess

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'.tools/app-catalog-pages-check'
OUT.mkdir(parents=True,exist_ok=True)
source=(ROOT/'components/ouse_installer/ouse_installer.c').read_text(encoding='utf-8')
section=source[source.index('    if(!strcmp(f->verb,"LIST")'):source.index('    if(!strcmp(f->verb,"BEGIN"))')]
fixture=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_SIZE 0x104
typedef struct {char id[48];} package_t;
static package_t packages[80],*installed[80];
static unsigned installed_capacity=80;
typedef struct {char *verb,*payload;} frame_t;
static bool number8(const char *text,uint32_t *value){if(strlen(text)!=8)return false;for(unsigned index=0;index<8;++index)if(!strchr("0123456789abcdef",text[index]))return false;*value=strtoul(text,NULL,16);return true;}
static int command(frame_t *f,char *result,size_t cap){
/* COMMAND */
 return -1;
}
int main(void){
 unsigned expected=0;for(unsigned index=0;index<80;++index)if(index%5){snprintf(packages[index].id,48,"user.large-app-count-%03u",index);installed[index]=&packages[index];++expected;}
 unsigned count=0,pages=0;uint32_t offset=0;char payload[9];unsigned char buffer[116];
 do{
  snprintf(payload,sizeof(payload),"%08lx",(unsigned long)offset);frame_t frame={"LIST_PAGE",payload};memset(buffer,0xa5,sizeof(buffer));
  assert(command(&frame,(char *)buffer,100)==ESP_OK);assert(strlen((char *)buffer)<100);for(unsigned index=100;index<sizeof(buffer);++index)assert(buffer[index]==0xa5);
  char *colon=strchr((char *)buffer,':');assert(colon==(char *)buffer+8);*colon=0;uint32_t next=strtoul((char *)buffer,NULL,16);assert(!next||next>offset);
  char *item=strtok(colon+1,",");while(item){assert(!strncmp(item,"user.large-app-count-",21));++count;item=strtok(NULL,",");}offset=next;++pages;assert(pages<80);
 }while(offset);
 assert(count==expected&&pages>1);frame_t legacy={"LIST","-"};assert(command(&legacy,(char *)buffer,100)==ESP_ERR_INVALID_SIZE);
 frame_t invalid={"LIST_PAGE","xyz"};assert(command(&invalid,(char *)buffer,100)==ESP_ERR_INVALID_ARG);
 puts("PASS catalog: 64 installed packages with holes, bounded multi-page progress, complete enumeration, legacy overflow rejection and buffer guards");
}
'''.replace('/* COMMAND */',section)
(OUT/'check.c').write_text(fixture)
env=dict(os.environ)
for line in subprocess.check_output(['cmd.exe','/d','/c',str(ROOT/'.tools/ouse-kernel-check/environment.cmd')],encoding='mbcs').splitlines():
    if '=' in line and not line.startswith('='):
        key,value=line.split('=',1)
        env[key]=value
compiler=Path(env['VCToolsInstallDir'])/'bin/Hostx64/x64/cl.exe'
result=subprocess.run([str(compiler),'/nologo','/std:c11','/O2','/MD',str(OUT/'check.c'),'/Fe'+str(OUT/'check.exe')],cwd=OUT,env=env,capture_output=True,text=True,encoding='utf-8',errors='replace')
assert result.returncode==0,(result.stdout,result.stderr)
subprocess.run([str(OUT/'check.exe')],cwd=OUT,check=True,timeout=15)
