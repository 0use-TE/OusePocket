"""Check the product scheduler overlay and native delay without building firmware."""
from pathlib import Path
import os, subprocess, hashlib, sys
sys.stdout.reconfigure(encoding='utf-8',errors='replace')
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'examples/brookesia-minimal/build/host-scheduler-wait'
OUT.mkdir(parents=True,exist_ok=True)
(OUT/'fixture').mkdir(exist_ok=True)
upstream=ROOT/'.reference/esp-brookesia/utils/brookesia_lib_utils/src/task_scheduler.cpp'
original=upstream.read_bytes();digest=hashlib.sha256(original).hexdigest()
cmake=Path('D:/Espressif/tools/cmake/4.0.3/bin/cmake.exe')
overlay=ROOT/'components/pocket_platform/cmake/scheduler_poll_wait.cmake'
script=OUT/'check.cmake'
script.write_text('''
set(CMAKE_CURRENT_BINARY_DIR "OUT_DIR")
function(idf_component_get_property result component property)
 if(property STREQUAL "COMPONENT_LIB")
  set(${result} utils PARENT_SCOPE)
 endif()
endfunction()
function(get_target_property result target property)
 set(${result} "SOURCE_PATH;other.cpp" PARENT_SCOPE)
endfunction()
function(get_source_file_property result source property)
 set(${result} "" PARENT_SCOPE)
endfunction()
macro(set_property)
 if("${ARGV0}" STREQUAL "TARGET")
  set(result_sources "${ARGV4}")
 endif()
endmacro()
function(target_include_directories)
endfunction()
include("OVERLAY_PATH")
file(READ "OUT_DIR/pocket_task_scheduler.cpp" generated)
string(FIND "${generated}" "::ouse::scheduler_poll_wait(poll_interval_ms);" pos)
if(pos EQUAL -1)
 message(FATAL_ERROR "Overlay did not replace scheduler wait")
endif()
'''.replace('OUT_DIR',OUT.as_posix()).replace('SOURCE_PATH',(OUT/'fixture/task_scheduler.cpp').as_posix()).replace('OVERLAY_PATH',overlay.as_posix()),encoding='utf-8')
fixture=OUT/'fixture/task_scheduler.cpp';fixture.write_bytes(original)
def configure(ok):
    p=subprocess.run([str(cmake),'-P',str(script)],capture_output=True,text=True,errors='replace')
    assert (p.returncode==0)==ok,(p.stdout,p.stderr)
configure(True)
fixture.write_bytes(original+original);configure(False)
fixture.write_text('void unrelated(){}',encoding='utf-8');configure(False)
assert hashlib.sha256(upstream.read_bytes()).hexdigest()==digest
print('PASS CMake overlay: one exact replacement, rejects missing/duplicate upstream code, reference unchanged')

# Actual header with fake FreeRTOS tick/delay transport only.
(OUT/'freertos').mkdir(exist_ok=True)
(OUT/'freertos/FreeRTOS.h').write_text('''#pragma once
#include <cstdint>
using TickType_t=uint32_t;
#define configTICK_RATE_HZ TEST_TICK_HZ
constexpr TickType_t portMAX_DELAY=0xffffffff;
''')
(OUT/'freertos/task.h').write_text('''#pragma once
inline TickType_t observed_delay=0;
inline void vTaskDelay(TickType_t ticks){observed_delay=ticks;}
''')
(OUT/'check.cpp').write_text('''#include "pocket_scheduler_wait.hpp"
#include <iostream>
#include <stdexcept>
int main(){
 for(uint32_t ms:{0u,1u,2u,5u,10u,1000u,0xffffffffu}) {
  ouse::scheduler_poll_wait(ms);
  auto ticks=(uint64_t(ms)*TEST_TICK_HZ+999)/1000;
  const auto expected=ticks==0?1:ticks>0xfffffffe?0xfffffffe:ticks;
  if(observed_delay!=expected || observed_delay==0 || observed_delay==portMAX_DELAY)
   throw std::runtime_error("Delay rounding/clamping failed");
 }
 std::cout<<"PASS native scheduler wait at "<<TEST_TICK_HZ<<"Hz: round up, minimum tick, overflow bound\\n";
}
''')
vswhere=Path(os.environ['ProgramFiles(x86)'])/'Microsoft Visual Studio/Installer/vswhere.exe'
vs=subprocess.check_output([str(vswhere),'-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],text=True).strip()
envscript=OUT/'environment.cmd';envscript.write_text(f'@echo off\ncall "{vs}/VC/Auxiliary/Build/vcvars64.bat" >nul\nset\n',encoding='mbcs')
env=dict(os.environ)
for line in subprocess.check_output(['cmd.exe','/d','/c',str(envscript)],encoding='mbcs').splitlines():
    if '=' in line and not line.startswith('='):key,value=line.split('=',1);env[key]=value
compiler=Path(env['VCToolsInstallDir'])/'bin/Hostx64/x64/cl.exe'
fixture.write_bytes(original)
# Also exercise real target/source properties during an actual host CMake
# configure/generate, rather than relying only on command stubs above.
mini=OUT/'cmake-project';mini.mkdir(exist_ok=True)
(mini/'other.cpp').write_text('void other(){}\n')
(mini/'CMakeLists.txt').write_text('''
cmake_minimum_required(VERSION 3.24)
project(scheduler_overlay_check CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_library(utils STATIC "SOURCE_PATH" other.cpp)
set_source_files_properties("SOURCE_PATH" PROPERTIES COMPILE_FLAGS "/Od")
function(idf_component_get_property result component property)
 set(${result} utils PARENT_SCOPE)
endfunction()
include("OVERLAY_PATH")
get_target_property(final_sources utils SOURCES)
if("SOURCE_PATH" IN_LIST final_sources)
 message(FATAL_ERROR "Original scheduler still compiled alongside replacement")
endif()
'''.replace('SOURCE_PATH',fixture.as_posix()).replace('OVERLAY_PATH',overlay.as_posix()))
generated=mini/'generated'
r=subprocess.run([str(cmake),'-G','Ninja','-S',str(mini),'-B',str(generated),
    '-DCMAKE_MAKE_PROGRAM=D:/Espressif/tools/ninja/1.12.1/ninja.exe',
    '-DCMAKE_CXX_COMPILER='+str(compiler)],env=env,capture_output=True,text=True,errors='replace')
assert r.returncode==0,(r.stdout,r.stderr)
commands=(generated/'compile_commands.json').read_text()
assert 'pocket_task_scheduler.cpp' in commands and '/Od' in commands
print('PASS real host CMake generation: replacement source selected, original removed, compiler flags retained')
for hz in [100,1000]:
    exe=OUT/f'wait-{hz}.exe'
    args=[str(compiler),'/nologo','/std:c++17','/EHsc',f'/DTEST_TICK_HZ={hz}',f'/I{OUT}',f'/I{ROOT/"components/pocket_platform/include"}',str(OUT/'check.cpp'),f'/Fe{exe}']
    r=subprocess.run(args,env=env,cwd=OUT,capture_output=True,text=True,errors='replace')
    if r.returncode:print(r.stdout);print(r.stderr);raise SystemExit(r.returncode)
    subprocess.run([str(exe)],check=True)
