"""Verify real CMake source/link selection and execute the patched thread setup."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'examples/brookesia-minimal/build/host-pthread-policy'
OUT.mkdir(parents=True, exist_ok=True)
UPSTREAM = ROOT / '.reference/esp-brookesia/utils/brookesia_lib_utils'
OVERLAY = ROOT / 'components/pocket_platform/cmake/pthread_memory_policy.cmake'
original = (UPSTREAM / 'src/thread_config.cpp').read_bytes()
digest = hashlib.sha256(original).hexdigest()

vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
vs = subprocess.check_output([str(vswhere), '-latest', '-products', '*', '-requires',
    'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], text=True).strip()
envscript = OUT / 'environment.cmd'
envscript.write_text(f'@echo off\ncall "{vs}/VC/Auxiliary/Build/vcvars64.bat" >nul\nset\n', encoding='mbcs')
env = dict(os.environ)
for line in subprocess.check_output(['cmd.exe', '/d', '/c', str(envscript)], encoding='mbcs').splitlines():
    if '=' in line and not line.startswith('='):
        key, value = line.split('=', 1)
        env[key] = value
compiler = Path(env['VCToolsInstallDir']) / 'bin/Hostx64/x64/cl.exe'
fixture = OUT / 'fixture'
fixture.mkdir(exist_ok=True)
(fixture / 'thread_config.cpp').write_bytes(original)
(fixture / 'thread_mutex.cpp').write_text('void unwanted_wrapper() {}\n')
(fixture / 'other.cpp').write_text('void retained_source() {}\n')
cmake = r'''
cmake_minimum_required(VERSION 3.24)
project(pthread_policy CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_library(utils STATIC "FIXTURE/thread_config.cpp" "FIXTURE/thread_mutex.cpp" "FIXTURE/other.cpp")
set_source_files_properties("FIXTURE/thread_config.cpp" PROPERTIES COMPILE_FLAGS "/Od")
target_link_options(utils INTERFACE "-Wl,--wrap=pthread_mutex_init" "-Wl,--wrap=pthread_mutex_destroy" "keep-option")
function(idf_component_get_property result component property)
 if(property STREQUAL "COMPONENT_LIB")
  set(${result} utils PARENT_SCOPE)
 else()
  set(${result} "FIXTURE" PARENT_SCOPE)
 endif()
endfunction()
include("OVERLAY")
get_target_property(options utils INTERFACE_LINK_OPTIONS)
if(NOT options STREQUAL "keep-option")
 message(FATAL_ERROR "Policy changed unrelated link options: ${options}")
endif()
'''.replace('FIXTURE', fixture.as_posix()).replace('OVERLAY', OVERLAY.as_posix())
(OUT / 'CMakeLists.txt').write_text(cmake)

def configure(expect_success=True):
    r = subprocess.run(['D:/Espressif/tools/cmake/4.0.3/bin/cmake.exe', '-G', 'Ninja',
        '-S', str(OUT), '-B', str(OUT / 'generated'),
        '-DCMAKE_BUILD_TYPE=Release',
        '-DCMAKE_MAKE_PROGRAM=D:/Espressif/tools/ninja/1.12.1/ninja.exe',
        '-DCMAKE_CXX_COMPILER=' + str(compiler)], env=env, capture_output=True, text=True, errors='replace')
    assert (r.returncode == 0) == expect_success, (r.stdout, r.stderr)

configure()
commands = json.loads((OUT / 'generated/compile_commands.json').read_text())
files = [Path(c['file']).name for c in commands]
assert files == ['pocket_thread_config.cpp', 'other.cpp'], files
assert '/Od' in commands[0]['command']
generated = (OUT / 'generated/pocket_thread_config.cpp').read_text()
start = generated.index('void ThreadConfig::apply() const')
end = generated.index('ThreadConfig ThreadConfig::get_system_default_config()', start)
body = generated[start:end]
fixture_cpp = r'''
#include <cassert>
#include <cstdint>
#include <string>
#define ESP_PLATFORM 1
#define CONFIG_SPIRAM TEST_SPIRAM
#define MALLOC_CAP_SPIRAM 0x400
#define MALLOC_CAP_INTERNAL 0x800
#define MALLOC_CAP_8BIT 4
#define CONFIG_PTHREAD_TASK_NAME_DEFAULT "pthread"
#define tskNO_AFFINITY -1
#define BROOKESIA_LOG_TRACE_GUARD_WITH_THIS()
#define BROOKESIA_CHECK_ESP_ERR_EXIT(value, message) assert((value)==0)
struct esp_pthread_cfg_t {
 size_t stack_size,prio; bool inherit_cfg; const char *thread_name;
 int pin_to_core; uint32_t stack_alloc_caps;
};
esp_pthread_cfg_t captured{};
esp_pthread_cfg_t esp_pthread_get_default_config() {
 return {4096,5,true,"default",-1,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT};
}
int esp_pthread_set_cfg(const esp_pthread_cfg_t *cfg) {captured=*cfg;return 0;}
struct ThreadConfig {
 std::string name; int core_id; size_t priority,stack_size; bool stack_in_ext;
 void apply() const;
};
BODY
int main() {
 for(int n=0;n<100;++n) {
  ThreadConfig config{"SvcMgrMain1",1,5,15360,false};config.apply();
  assert(!captured.inherit_cfg && captured.pin_to_core==1 && captured.prio==5);
  assert(captured.stack_size==15360 && std::string(captured.thread_name)==config.name);
  assert(captured.stack_alloc_caps==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
  config.name="";config.core_id=-1;config.stack_in_ext=true;config.apply();
  assert(!captured.inherit_cfg && captured.pin_to_core==-1);
  assert(std::string(captured.thread_name)=="pthread");
  assert(captured.stack_alloc_caps==((TEST_SPIRAM?MALLOC_CAP_SPIRAM:MALLOC_CAP_INTERNAL)|MALLOC_CAP_8BIT));
 }
}
'''.replace('BODY', body)
(OUT / 'check.cpp').write_text(fixture_cpp)
for spiram in [0, 1]:
    exe = OUT / f'check-{spiram}.exe'
    r = subprocess.run([str(compiler), '/nologo', '/std:c++17', '/EHsc', f'/DTEST_SPIRAM={spiram}',
        str(OUT / 'check.cpp'), f'/Fe{exe}'], env=env, cwd=OUT, capture_output=True, text=True, errors='replace')
    assert r.returncode == 0, (r.stdout, r.stderr)
    subprocess.run([str(exe)], check=True)

for content in [original.replace(b'esp_pthread_cfg_t new_cfg;', b'esp_pthread_cfg_t changed;'),
                original + b'\nesp_pthread_cfg_t new_cfg;\n']:
    (fixture / 'thread_config.cpp').write_bytes(content)
    configure(False)
(fixture / 'thread_config.cpp').write_bytes(original)
configure()
assert hashlib.sha256((UPSTREAM / 'src/thread_config.cpp').read_bytes()).hexdigest() == digest
print('PASS: real CMake selects initialized config, removes mutex wrapper and preserves unrelated flags')
print('PASS: actual ThreadConfig::apply handles internal/PSRAM stacks and deterministic inheritance')
print('PASS: changed or duplicate upstream initialization fails configuration; reference source unchanged')
