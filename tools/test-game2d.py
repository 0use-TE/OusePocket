"""Real QuickJS binding + production C++ kernel: parity, limits and timings."""
from pathlib import Path
import os,subprocess
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'.tools/game2d-check';OUT.mkdir(parents=True,exist_ok=True)
env=dict(os.environ)
for line in subprocess.check_output(['cmd.exe','/d','/c',str(ROOT/'.tools/ouse-core-check/environment.cmd')],encoding='mbcs').splitlines():
 if '=' in line and not line.startswith('='):
  k,v=line.split('=',1);env[k]=v
cl=Path(env['VCToolsInstallDir'])/'bin/Hostx64/x64/cl.exe'
runtime=ROOT/'components/ouse_runtime';qjs=ROOT/'managed_components/espressif__quickjs-ng/quickjs-ng'
fixture=r'''
#include <windows.h>
#include "quickjs.h"
#include "ouse_game2d.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include "ouse_runtime_game2d.inc"
static JSValue print(JSContext *c,JSValueConst self,int n,JSValueConst *a){(void)self;for(int i=0;i<n;i++){const char *s=JS_ToCString(c,a[i]);puts(s?s:"<exception>");JS_FreeCString(c,s);}return JS_UNDEFINED;}
static JSValue now(JSContext *c,JSValueConst self,int n,JSValueConst *a){(void)self;(void)n;(void)a;LARGE_INTEGER t,f;QueryPerformanceCounter(&t);QueryPerformanceFrequency(&f);return JS_NewFloat64(c,(double)t.QuadPart*1000/f.QuadPart);}
static JSValue detach(JSContext *c,JSValueConst self,int n,JSValueConst *a){(void)self;assert(n==1);JS_DetachArrayBuffer(c,a[0]);return JS_UNDEFINED;}
int main(int argc,char **argv){assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);char *s=malloc(n+1);assert(s&&fread(s,1,n,f)==n);s[n]=0;fclose(f);
 unsigned char raw[80],target[48],result[16];double shot[8]={0,0,1200,0,1,1,1,0},body[4]={10,0,1,1};uint32_t mask;
 for(unsigned alignment=1;alignment<=7;++alignment){memcpy(raw+alignment,shot,sizeof(shot));memcpy(target+alignment,body,sizeof(body));assert(ouse_game2d_projectiles(raw+alignment,1,target+alignment,1,result+alignment,1.0/60));memcpy(&mask,result+alignment,4);memcpy(shot,raw+alignment,sizeof(shot));assert(mask==1&&shot[0]==20);shot[0]=0;}
 puts("PASS byte-aligned buffers at offsets 1..7 (including ESP32-P4 4-byte alignment)");
 JSRuntime *r=JS_NewRuntime();JSContext *c=JS_NewContext(r);JS_SetMemoryLimit(r,32*1024*1024);JSValue g=JS_GetGlobalObject(c);
 JS_SetPropertyStr(c,g,"nativeStep",JS_NewCFunction(c,game2d_projectiles,"nativeStep",4));JS_SetPropertyStr(c,g,"print",JS_NewCFunction(c,print,"print",1));JS_SetPropertyStr(c,g,"now",JS_NewCFunction(c,now,"now",0));JS_FreeValue(c,g);
 g=JS_GetGlobalObject(c);JS_SetPropertyStr(c,g,"detachBuffer",JS_NewCFunction(c,detach,"detachBuffer",1));JS_FreeValue(c,g);
 JSValue v=JS_Eval(c,s,n,argv[1],JS_EVAL_TYPE_GLOBAL);int failed=JS_IsException(v);if(failed){JSValue e=JS_GetException(c);const char *t=JS_ToCString(c,e);fprintf(stderr,"%s\n",t);JS_FreeCString(c,t);JS_FreeValue(c,e);}JS_FreeValue(c,v);free(s);JS_FreeContext(c);JS_FreeRuntime(r);return failed;
}
'''
(OUT/'check.c').write_text(fixture,encoding='utf-8')
base=[str(cl),'/nologo','/O2','/MD','/utf-8','/I'+str(runtime),'/I'+str(runtime/'include'),'/I'+str(qjs)]
subprocess.run(base+['/c','/std:c++17','/EHsc',str(runtime/'ouse_game2d.cpp'),'/Fo'+str(OUT/'kernel.obj')],cwd=OUT,env=env,check=True)
subprocess.run(base+['/std:c11',str(OUT/'check.c'),str(OUT/'kernel.obj'),str(ROOT/'.tools/ouse-runtime-check/quickjs-native.lib'),'/Fe'+str(OUT/'check.exe')],cwd=OUT,env=env,check=True)
script=(ROOT/'tools/fixtures/game2d.js').read_text(encoding='utf-8')+'\n'+(ROOT/'apps/survivor/engine.js').read_text(encoding='utf-8')+r'''
for(let gun=0;gun<6;gun++){
 globalThis.Ouse={game2d:{version:1,stepProjectiles:nativeStep}};const a=createSurvivorEngine(101+gun);delete globalThis.Ouse;const b=createSurvivorEngine(101+gun);
 a.reset({gun,pet:gun%4});b.reset({gun,pet:gun%4});
 for(let step=0;step<1800;step++){
  for(const e of [a,b]){if(e.state.mode==='upgrade')e.choose(0);e.move(Math.sin(step*.01),Math.cos(step*.02));if(step%180===0)e.skill();e.advance(1/60);}
  assert(JSON.stringify(a.state)===JSON.stringify(b.state),'whole-game parity gun '+gun+' step '+step);
  assert(JSON.stringify(a.drainEvents())===JSON.stringify(b.drainEvents()),'events');
 }
}
print('PASS six weapons: native and JS gameplay/state/events identical over 10,800 simulation steps');
'''
(OUT/'check.js').write_text(script,encoding='utf-8')
result=subprocess.run([str(OUT/'check.exe'),str(OUT/'check.js')],cwd=OUT,capture_output=True,text=True,timeout=120)
(OUT/'result.txt').write_text(result.stdout+result.stderr,encoding='utf-8')
print(result.stdout,end='');assert result.returncode==0,result.stderr
