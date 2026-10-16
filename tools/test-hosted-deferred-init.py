"""Exercise the Hosted source override with native CMake and whole-archive links.

Only host fixtures are built. The managed dependency and device firmware are
never changed or compiled by this check.
"""
from pathlib import Path
import hashlib
import json
import os
import re
import subprocess
import sys

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
ROOT = Path(__file__).resolve().parents[1]
HOSTED = ROOT / "managed_components/espressif__esp_hosted"
UPSTREAM = HOSTED / "host/port/esp/freertos/src/port_esp_hosted_host_init.c"
OVERLAY = ROOT / "boards/waveshare_lcd35/cmake/hosted_deferred_init.cmake"
OUT = ROOT / ".tools/hosted-deferred-init-check"
FIXTURE = OUT / "fixture"
BUILD = OUT / "generated-msvc"
CM = Path("D:/Espressif/tools/cmake/4.0.3/bin/cmake.exe")
NINJA = Path("D:/Espressif/tools/ninja/1.12.1/ninja.exe")
original = UPSTREAM.read_bytes()
digest = hashlib.sha256(original).hexdigest()
FIXTURE.mkdir(parents=True, exist_ok=True)
hook = FIXTURE / UPSTREAM.name
hook.write_bytes(original)
other = FIXTURE / "other.c"
other_original = "#ifndef TARGET_PROPERTY_PRESERVED\n#error target definitions lost\n#endif\nvoid unrelated_hosted_source(void) {}\n"
other.write_text(other_original, encoding="utf-8")
headers = FIXTURE / "include"
(headers / "esp_private").mkdir(parents=True, exist_ok=True)
(headers / "esp_log.h").write_text("#define ESP_LOGI(...) ((void)0)\n", encoding="utf-8")
(headers / "port_esp_hosted_host_log.h").write_text(
    '#define DEFINE_LOG_TAG(name) static const char *TAG = #name\n', encoding="utf-8"
)
(headers / "esp_private/startup_internal.h").write_text("/* Host fixture. */\n", encoding="utf-8")
(headers / "esp_hosted.h").write_text(
    '#include <stdlib.h>\n#ifdef __cplusplus\nextern "C" {\n#endif\n'
    "int esp_hosted_init(void);\nint esp_hosted_deinit(void);\n"
    "#ifdef __cplusplus\n}\n#endif\n"
    "#define ESP_ERROR_CHECK(expr) do { if ((expr) != 0) abort(); } while (0)\n",
    encoding="utf-8",
)
(FIXTURE / "main.c").write_text(r'''
#include <stdio.h>
#include "esp_hosted.h"
static int scheduler_running;
static int init_count;
static int before_scheduler_count;
int esp_hosted_init(void) {
    ++init_count;
    if (!scheduler_running) ++before_scheduler_count;
    printf("HOST_INIT scheduler=%d\n", scheduler_running);
    return 0;
}
int esp_hosted_deinit(void) {
    printf("HOST_DEINIT scheduler=%d\n", scheduler_running);
    return 0;
}
int main(void) {
    if (init_count != EXPECT_AUTO || before_scheduler_count != EXPECT_AUTO) return 10;
    puts("MAIN_ENTERED");
    scheduler_running = 1;
    if (!EXPECT_AUTO && esp_hosted_init() != 0) return 11;
    if (init_count != 1 || before_scheduler_count != EXPECT_AUTO) return 12;
    puts("MANUAL_START_OK");
    return 0;
}
''', encoding="utf-8")

# MSVC has no GNU constructor attribute. The control below calls the exact
# upstream function bodies from an equivalent C++ static lifecycle object;
# generated product C is compiled directly, without any such adapter.
(FIXTURE / "upstream_host.cpp").write_text(r'''
#define __attribute__(...) /* GNU lifecycle registration supplied below. */
#include "port_esp_hosted_host_init.c"
struct UpstreamLifecycle {
    UpstreamLifecycle() { esp_hosted_host_init(); }
    ~UpstreamLifecycle() { esp_hosted_host_deinit(); }
};
static UpstreamLifecycle upstream_lifecycle;
''', encoding="utf-8")
vswhere = Path(os.environ["ProgramFiles(x86)"]) / "Microsoft Visual Studio/Installer/vswhere.exe"
vs = subprocess.check_output([
    str(vswhere), "-latest", "-products", "*", "-requires",
    "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath",
], text=True).strip()
assert vs, "A Visual Studio C/C++ host toolchain is required"
envscript = OUT / "environment.cmd"
envscript.write_text(
    f'@echo off\ncall "{vs}/VC/Auxiliary/Build/vcvars64.bat" >nul\nset\n', encoding="mbcs"
)
env = dict(os.environ)
for line in subprocess.check_output(["cmd.exe", "/d", "/c", str(envscript)], encoding="mbcs").splitlines():
    if "=" in line and not line.startswith("="):
        key, value = line.split("=", 1)
        env[key] = value
CC = Path(env["VCToolsInstallDir"]) / "bin/Hostx64/x64/cl.exe"

project = r'''
cmake_minimum_required(VERSION 3.24)
project(hosted_deferred_init C CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CONFIG_ESP_HOSTED_ENABLED y)
if(TEST_CASE STREQUAL "disabled")
 set(CONFIG_ESP_HOSTED_ENABLED n)
endif()
set(hook "FIXTURE/port_esp_hosted_host_init.c")
set(sources "${hook}" "FIXTURE/other.c")
if(TEST_CASE STREQUAL "missing")
 set(sources "FIXTURE/other.c")
elseif(TEST_CASE STREQUAL "duplicate")
 list(APPEND sources "${hook}")
elseif(TEST_CASE STREQUAL "relative")
 set(sources "port_esp_hosted_host_init.c" "other.c")
endif()
add_library(hosted STATIC ${sources})
target_include_directories(hosted PUBLIC "FIXTURE/include")
target_compile_definitions(hosted PRIVATE TARGET_PROPERTY_PRESERVED=1)
set_target_properties(hosted PROPERTIES OUTPUT_NAME retained_hosted)
set_property(TARGET hosted PROPERTY TEST_WHOLE_ARCHIVE TRUE)
set_source_files_properties("${hook}" PROPERTIES
 COMPILE_FLAGS "/DSOURCE_FLAGS_PRESERVED=1"
 COMPILE_OPTIONS "/DSOURCE_OPTIONS_PRESERVED=1"
 COMPILE_DEFINITIONS "SOURCE_DEFINITIONS_PRESERVED=1"
 INCLUDE_DIRECTORIES "FIXTURE/source_include")
function(idf_build_get_property output property)
 if(NOT property STREQUAL "BUILD_COMPONENTS")
  message(FATAL_ERROR "Unexpected build property")
 endif()
 set(${output} "${TEST_COMPONENT_NAME}" PARENT_SCOPE)
endfunction()
function(idf_component_get_property output component property)
 if(NOT component STREQUAL TEST_COMPONENT_NAME)
  message(FATAL_ERROR "Incorrect component name: ${component}")
 endif()
 if(property STREQUAL "COMPONENT_LIB")
  set(${output} hosted PARENT_SCOPE)
 elseif(property STREQUAL "COMPONENT_DIR")
  set(${output} "FIXTURE" PARENT_SCOPE)
 elseif(property STREQUAL "WHOLE_ARCHIVE")
  get_target_property(whole hosted TEST_WHOLE_ARCHIVE)
  set(${output} "${whole}" PARENT_SCOPE)
 else()
  message(FATAL_ERROR "Unexpected component property")
 endif()
endfunction()
include("OVERLAY")
get_target_property(actual hosted SOURCES)
file(WRITE "${CMAKE_BINARY_DIR}/selected.txt" "${actual}")
get_target_property(output_name hosted OUTPUT_NAME)
get_target_property(definitions hosted COMPILE_DEFINITIONS)
get_target_property(includes hosted INCLUDE_DIRECTORIES)
idf_component_get_property(whole "${TEST_COMPONENT_NAME}" WHOLE_ARCHIVE)
if(NOT output_name STREQUAL "retained_hosted" OR NOT whole
 OR NOT definitions STREQUAL "TARGET_PROPERTY_PRESERVED=1"
 OR NOT includes STREQUAL "FIXTURE/include")
 message(FATAL_ERROR "Hosted target properties were changed")
endif()
add_executable(manual "FIXTURE/main.c")
target_compile_definitions(manual PRIVATE EXPECT_AUTO=0)
target_link_libraries(manual PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,hosted>")

# Positive control: the exact upstream file really does invoke Hosted before
# main even though nobody references its static lifecycle functions.
add_library(upstream STATIC "FIXTURE/upstream_host.cpp")
target_include_directories(upstream PUBLIC "FIXTURE/include")
add_executable(automatic "FIXTURE/main.c")
target_compile_definitions(automatic PRIVATE EXPECT_AUTO=1)
target_link_libraries(automatic PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,upstream>")
'''.replace("FIXTURE", FIXTURE.as_posix()).replace("OVERLAY", OVERLAY.as_posix())
(OUT / "CMakeLists.txt").write_text(project, encoding="utf-8")
(FIXTURE / "source_include").mkdir(exist_ok=True)


def configure(case="normal", component="espressif__esp_hosted", ok=True):
    result = subprocess.run([
        str(CM), "-S", str(OUT), "-B", str(BUILD), "-G", "Ninja",
        "-DCMAKE_MAKE_PROGRAM=" + str(NINJA), "-DCMAKE_C_COMPILER=" + str(CC),
        "-DCMAKE_CXX_COMPILER=" + str(CC), "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_TRY_COMPILE_CONFIGURATION=Release",
        "-DTEST_CASE=" + case, "-DTEST_COMPONENT_NAME=" + component,
    ], env=env, capture_output=True, text=True, errors="replace", timeout=90)
    assert (result.returncode == 0) == ok, result.stdout + result.stderr
    if not ok:
        assert "Hosted" in result.stdout + result.stderr or "esp_hosted" in result.stdout + result.stderr


configure()
selected = (BUILD / "selected.txt").read_text().split(";")
generated = BUILD / "ouse_hosted/port_esp_hosted_host_init.c"
assert selected == [generated.as_posix(), other.as_posix()], selected
commands = json.loads((BUILD / "compile_commands.json").read_text())
generated_command = next(c["command"] for c in commands if Path(c["file"]) == generated)
for preserved in ("SOURCE_FLAGS_PRESERVED", "SOURCE_OPTIONS_PRESERVED", "SOURCE_DEFINITIONS_PRESERVED", "source_include"):
    assert preserved in generated_command, generated_command
subprocess.run([str(CM), "--build", str(BUILD)], env=env, check=True, capture_output=True, text=True, timeout=90)
manual = subprocess.check_output([str(BUILD / "manual.exe")], text=True).splitlines()
automatic = subprocess.check_output([str(BUILD / "automatic.exe")], text=True).splitlines()
assert manual == ["MAIN_ENTERED", "HOST_INIT scheduler=1", "MANUAL_START_OK"], manual
assert automatic == ["HOST_INIT scheduler=0", "MAIN_ENTERED", "MANUAL_START_OK", "HOST_DEINIT scheduler=1"], automatic

# Resolve both ordinary local component names and Component Manager names.
configure(component="esp_hosted")
configure("relative")
for case in ("missing", "duplicate", "disabled"):
    configure(case, ok=False)
configure(component="unrelated_component", ok=False)
for broken in (
    original + b"\nvoid newly_added_behavior(void) {}\n",
    original.replace(b"((constructor))", b"((constructor(101)))"),
    original + b"\nstatic void __attribute__((constructor)) another_init(void) {}\n",
):
    hook.write_bytes(broken)
    configure(ok=False)
hook.write_bytes(original)
for extra in (
    "static void __attribute__((constructor)) extra_init(void) {}\n",
    "ESP_SYSTEM_INIT_FN(extra_init, CORE, 1, 1) {}\n",
):
    other.write_text(other_original + extra, encoding="utf-8")
    configure(ok=False)
other.write_text(other_original, encoding="utf-8")
configure()

# Audit the actual installed host sources, not just a synthetic API fixture.
# Wi-Fi weak APIs must not introduce a second implicit Hosted initializer.
def executable_text(path):
    text = path.read_text(encoding="utf-8")
    return re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"', "", text, flags=re.S)


init_sites = []
automatic_sources = []
for path in sorted([*(HOSTED / "host").rglob("*.c"), *(HOSTED / "common").rglob("*.c")]):
    text = executable_text(path)
    if re.search(r"\besp_hosted_init\s*\(", text):
        init_sites.append(path.relative_to(HOSTED).as_posix())
    if re.search(r"\bconstructor\b|\bESP_SYSTEM_INIT_FN\s*\(", text):
        automatic_sources.append(path.relative_to(HOSTED).as_posix())
assert sorted(init_sites) == ["host/api/src/esp_hosted_api.c", "host/port/esp/freertos/src/port_esp_hosted_host_init.c"], init_sites
assert automatic_sources == ["host/port/esp/freertos/src/port_esp_hosted_host_init.c"], automatic_sources
sdk = Path(os.environ.get("IDF_PATH", "D:/esp/v6.1/esp-idf"))
for remote in (sdk / "components/esp_wifi/remote", HOSTED.parent / "espressif__esp_wifi_remote"):
    for path in remote.rglob("*.c"):
        text = executable_text(path)
        assert not re.search(r"\besp_hosted_init\s*\(|\bconstructor\b|\bESP_SYSTEM_INIT_FN\s*\(", text), path
assert hashlib.sha256(UPSTREAM.read_bytes()).hexdigest() == digest
print("PASS: whole-archive upstream initializes before main; product initializes only after scheduler; properties and managed source preserved; upstream drift rejected")
