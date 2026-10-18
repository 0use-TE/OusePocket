"""Compile the production silent AVI muxer and validate its binary contract."""
import os
from pathlib import Path
import struct
import subprocess

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'.tools/camera-avi-check'
OUT.mkdir(parents=True,exist_ok=True)
vswhere=Path(os.environ['ProgramFiles(x86)'])/'Microsoft Visual Studio/Installer/vswhere.exe'
installation=subprocess.check_output([str(vswhere),'-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],text=True).strip()
environment=OUT/'environment.cmd'
environment.write_text(f'@echo off\ncall "{installation}/VC/Auxiliary/Build/vcvars64.bat" >nul\nset\n',encoding='mbcs')
env=dict(os.environ)
for line in subprocess.check_output(['cmd.exe','/d','/c',str(environment)],encoding='mbcs').splitlines():
    if '=' in line and not line.startswith('='):
        key,value=line.split('=',1)
        env[key]=value
compiler=Path(env['VCToolsInstallDir'])/'bin/Hostx64/x64/cl.exe'
source=OUT/'avi-test.c'
record=(ROOT/'components/ouse_camera/ouse_camera_record.inc').read_text(encoding='utf-8')
scaler=record[record.index('static const unsigned char *record_input('):record.index('static void record_frame(')]
source.write_text(r'''
#include "ouse_avi.h"
#include <assert.h>
#include <string.h>
typedef struct {const unsigned char *pixels;unsigned width,height,stride;} ouse_camera_frame_t;
static struct {unsigned char *input;unsigned width,height;} recording;
/* SCALER */
int main(void)
{
    uint16_t pixels[80],scaled[16];for(unsigned index=0;index<80;++index)pixels[index]=index;
    ouse_camera_frame_t frame={.pixels=(const unsigned char *)pixels,.width=8,.height=8,.stride=10};assert(record_input(&frame)==frame.pixels);
    recording.input=(unsigned char *)scaled;recording.width=recording.height=4;assert(record_input(&frame)==recording.input);
    assert(scaled[0]==0 && scaled[1]==2 && scaled[4]==20 && scaled[15]==66);
    FILE *file=fopen("fixture.avi","w+b");assert(file);
    ouse_avi_writer_t writer;ouse_avi_index_t index[3];
    const unsigned char even[]={255,216,1,2,255,217},odd[]={255,216,3,255,217};
    assert(!ouse_avi_begin(&writer,file,0,240,index,3));
    assert(ouse_avi_begin(&writer,file,320,240,index,3));assert(!ouse_avi_finish(&writer,0));
    assert(!ouse_avi_frame(&writer,"bad",3));assert(ouse_avi_frame(&writer,even,sizeof(even)));assert(ouse_avi_frame(&writer,odd,sizeof(odd)));assert(ouse_avi_frame(&writer,even,sizeof(even)));
    assert(!ouse_avi_frame(&writer,even,sizeof(even)));assert(ouse_avi_finish(&writer,450000));
    ouse_avi_info_t info;assert(ouse_avi_read_info(file,&info));assert(info.width==320 && info.height==240 && info.interval_us==150000);
    unsigned char output[32];uint32_t length=0;
    assert(ouse_avi_read_frame(file,&info,output,sizeof(output),&length) && length==sizeof(even) && !memcmp(output,even,length));
    assert(ouse_avi_read_frame(file,&info,output,sizeof(output),&length) && length==sizeof(odd) && !memcmp(output,odd,length));
    assert(ouse_avi_read_frame(file,&info,output,sizeof(output),&length));assert(!ouse_avi_read_frame(file,&info,output,sizeof(output),&length));
    assert(!fseek(file,224,SEEK_SET));assert(!ouse_avi_read_frame(file,&info,output,2,&length));
    assert(!fseek(file,228,SEEK_SET));assert(fwrite("\xff\xff\xff\x7f",1,4,file)==4);assert(!fseek(file,224,SEEK_SET));assert(!ouse_avi_read_frame(file,&info,output,sizeof(output),&length));
    assert(!fseek(file,228,SEEK_SET));assert(fwrite("\x06\0\0\0",1,4,file)==4);assert(!fclose(file));
    file=fopen("truncated.avi","w+b");assert(file);assert(fwrite("RIFF",1,4,file)==4);assert(!ouse_avi_read_info(file,&info));fclose(file);
    unsigned char large[10003];memset(large,42,sizeof(large));large[0]=255;large[1]=216;large[sizeof(large)-2]=255;large[sizeof(large)-1]=217;
    file=fopen("large.avi","w+b");assert(file);assert(ouse_avi_begin(&writer,file,320,240,index,3));assert(ouse_avi_frame(&writer,large,sizeof(large)));assert(ouse_avi_finish(&writer,100000));
    assert(ouse_avi_read_info(file,&info));unsigned char restored[10003];assert(ouse_avi_read_frame(file,&info,restored,sizeof(restored),&length));assert(length==sizeof(large) && !memcmp(large,restored,length));fclose(file);
    return 0;
}
'''.replace('/* SCALER */',scaler),encoding='utf-8')
command=[str(compiler),'/nologo','/std:c11','/O2','/MD','/utf-8','/I'+str(ROOT/'components/ouse_camera'),str(source),str(ROOT/'components/ouse_camera/ouse_avi.c'),'/Fe'+str(OUT/'avi-test.exe')]
result=subprocess.run(command,cwd=OUT,env=env,capture_output=True,text=True,errors='replace')
assert result.returncode==0,(result.stdout,result.stderr)
subprocess.run([str(OUT/'avi-test.exe')],cwd=OUT,check=True,timeout=10)
data=(OUT/'fixture.avi').read_bytes()
def number(offset):
    return struct.unpack_from('<I',data,offset)[0]
assert number(4)==len(data)-8
assert number(48)==3 and number(56)==1
assert number(128)==150000 and number(132)==1000000
assert data[108:116]==b'vidsMJPG'
position=224
for frame in range(3):
    assert data[position:position+4]==b'00dc'
    length=number(position+4)
    position+=8+length+(length&1)
assert number(216)==position-220 and data[position:position+4]==b'idx1'
assert number(position+4)==48
assert struct.unpack_from('<4I',data,position+8+4)==(16,4,6,int.from_bytes(b'00dc','little'))
print('PASS production AVI: one silent MJPEG stream, actual duration, complete RIFF/movi/index, odd padding, frame limits, invalid JPEG, bounds and truncation checks')
