"""Validate the product adapter using real CMake and actual profiler sample/math."""
from pathlib import Path
import hashlib, json, os, re, subprocess, sys
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
ROOT = Path(__file__).resolve().parents[1]
UPSTREAM = ROOT / ".reference/esp-brookesia/utils/brookesia_lib_utils"
OVERLAY = ROOT / "components/pocket_platform/cmake/thread_profiler_runtime.cmake"
OUT = ROOT / ".tools/thread-profiler-check"
OUT.mkdir(parents=True, exist_ok=True)
original = (UPSTREAM / "src/thread_profiler.cpp").read_bytes()
header = (UPSTREAM / "include/brookesia/lib_utils/thread_profiler.hpp").read_text()
digest = hashlib.sha256(original).hexdigest()
fixture = OUT / "fixture"
fixture.mkdir(exist_ok=True)
(fixture / "thread_profiler.cpp").write_bytes(original)
(fixture / "other.cpp").write_text("void retained() {}\n")
vswhere = Path(os.environ["ProgramFiles(x86)"]) / "Microsoft Visual Studio/Installer/vswhere.exe"
vs = subprocess.check_output([str(vswhere), "-latest", "-products", "*", "-requires",
    "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"], text=True).strip()
envscript = OUT / "environment.cmd"
envscript.write_text(f'@echo off\ncall "{vs}/VC/Auxiliary/Build/vcvars64.bat" >nul\nset\n', encoding="mbcs")
env = dict(os.environ)
for line in subprocess.check_output(["cmd.exe", "/d", "/c", str(envscript)], encoding="mbcs").splitlines():
    if "=" in line and not line.startswith("="):
        key, value = line.split("=", 1)
        env[key] = value
cc = Path(env["VCToolsInstallDir"]) / "bin/Hostx64/x64/cl.exe"
project = r"""
cmake_minimum_required(VERSION 3.24)
project(profiler_adapter CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_library(utils STATIC "FIXTURE/thread_profiler.cpp" "FIXTURE/other.cpp")
set_source_files_properties("FIXTURE/thread_profiler.cpp" PROPERTIES COMPILE_FLAGS "/Od")
function(idf_component_get_property output component property)
 if(property STREQUAL "COMPONENT_LIB")
  set(${output} utils PARENT_SCOPE)
 else()
  set(${output} "FIXTURE" PARENT_SCOPE)
 endif()
endfunction()
include("OVERLAY")
""".replace("FIXTURE", fixture.as_posix()).replace("OVERLAY", OVERLAY.as_posix())
(OUT / "CMakeLists.txt").write_text(project)

def configure(ok=True):
    p = subprocess.run(["D:/Espressif/tools/cmake/4.0.3/bin/cmake.exe", "-S", str(OUT),
        "-B", str(OUT / "generated"), "-G", "Ninja",
        "-DCMAKE_MAKE_PROGRAM=D:/Espressif/tools/ninja/1.12.1/ninja.exe",
        "-DCMAKE_CXX_COMPILER=" + str(cc), "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_TRY_COMPILE_CONFIGURATION=Release"], env=env, capture_output=True,
        text=True, errors="replace", timeout=90)
    assert (p.returncode == 0) == ok, (p.stdout, p.stderr)

configure()
generated = (OUT / "generated/ouse_profiler/thread_profiler.cpp").read_text()
commands = json.loads((OUT / "generated/compile_commands.json").read_text())
assert [Path(c["file"]).name for c in commands] == ["thread_profiler.cpp", "other.cpp"]
assert Path(commands[0]["file"]).parent.name == "ouse_profiler"
assert "/Od" in commands[0]["command"]
for text in (original + original, original.replace(b"&result->runtime", b"&changed")):
    (fixture / "thread_profiler.cpp").write_bytes(text)
    configure(False)
(fixture / "thread_profiler.cpp").write_bytes(original)
configure()
assert hashlib.sha256((UPSTREAM / "src/thread_profiler.cpp").read_bytes()).hexdigest() == digest

declarations = []
for name in ("TaskState", "TaskStatus", "TaskInfo", "Statistics", "ProfileSnapshot", "SampleResult"):
    match = re.search(r"    (?:enum class|struct) " + name + r"\s*\{.*?\n    \};", header, re.S)
    assert match, name
    declarations.append(match.group())
start = generated.index("std::shared_ptr<ThreadProfiler::SampleResult> ThreadProfiler::sample_tasks()")
end = generated.index("\nvoid ThreadProfiler::sort_tasks(", start)
methods = generated[start:end]
body = r"""
#include <cassert>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <type_traits>
#include <iostream>
using configRUN_TIME_COUNTER_TYPE = std::conditional_t<TEST_BITS == 64, uint64_t, uint32_t>;
using UBaseType_t = uint32_t;
using BaseType_t = int;
using TaskHandle_t = void *;
enum eTaskState { eRunning, eReady, eBlocked, eSuspended, eDeleted, eInvalid };
struct TaskStatus_t {
    TaskHandle_t xHandle;
    const char *pcTaskName;
    eTaskState eCurrentState;
    uint32_t uxCurrentPriority;
    BaseType_t xCoreID;
    uint32_t usStackHighWaterMark;
    configRUN_TIME_COUNTER_TYPE ulRunTimeCounter;
};
#define portNUM_PROCESSORS 2
#define tskNO_AFFINITY -1
#define BROOKESIA_UTILS_THREAD_PROFILER_AVAILABLE 1
#define BROOKESIA_LOG_TRACE_GUARD()
#define BROOKESIA_CHECK_EXCEPTION_RETURN(expr, ret, ...) do { expr; } while (0)
#define BROOKESIA_CHECK_FALSE_RETURN(cond, ret, ...) do { if (!(cond)) return ret; } while (0)
static configRUN_TIME_COUNTER_TYPE mock_runtime;
static TaskStatus_t mock_task;
static UBaseType_t uxTaskGetNumberOfTasks() { return 1; }
static UBaseType_t uxTaskGetSystemState(TaskStatus_t *tasks, UBaseType_t size, configRUN_TIME_COUNTER_TYPE *total) {
    assert(size == 1); *total = mock_runtime; tasks[0] = mock_task; return 1;
}
static void *task_stack_start(TaskHandle_t handle) { return handle; }
static bool esp_ptr_internal(const void *) { return true; }
class ThreadProfiler {
public:
DECLARATIONS
    static std::shared_ptr<SampleResult> sample_tasks();
    static std::shared_ptr<ProfileSnapshot> take_snapshot(const SampleResult &, const SampleResult &);
    static TaskState convert_task_state(eTaskState state) { return static_cast<TaskState>(state); }
};
METHODS
int main() {
    static_assert(sizeof(ThreadProfiler::SampleResult::runtime) == 4, "Keep public sample ABI");
    static_assert(sizeof(configRUN_TIME_COUNTER_TYPE) == TEST_BITS / 8);
    mock_task = {reinterpret_cast<void *>(0x1000), "worker", eRunning, 5, 1, 1024, 0};
    // Total and task counters both cross their low 32-bit boundary while sampling.
    auto check = [](uint64_t begin_total, uint64_t begin_task, uint32_t interval, uint32_t busy, uint32_t percent) {
        mock_runtime = static_cast<configRUN_TIME_COUNTER_TYPE>(begin_total);
        mock_task.ulRunTimeCounter = static_cast<configRUN_TIME_COUNTER_TYPE>(begin_task);
        auto a = ThreadProfiler::sample_tasks();
        assert(a->runtime == static_cast<uint32_t>(begin_total));
        mock_runtime = static_cast<configRUN_TIME_COUNTER_TYPE>(begin_total + interval);
        mock_task.ulRunTimeCounter = static_cast<configRUN_TIME_COUNTER_TYPE>(begin_task + busy);
        auto b = ThreadProfiler::sample_tasks();
        b->timestamp = a->timestamp + std::chrono::microseconds(interval);
        auto snapshot = ThreadProfiler::take_snapshot(*a, *b);
        assert(snapshot && snapshot->tasks.size() == 1);
        assert(snapshot->tasks[0].elapsed_time == busy);
        assert(snapshot->tasks[0].runtime_counter == static_cast<uint32_t>(begin_task + busy));
        assert(snapshot->tasks[0].cpu_percent == percent);
        assert(snapshot->stats.total_cpu_percent == percent);
        assert(snapshot->total_runtime == b->runtime);
    };
    check((1ULL<<32)-500000, (1ULL<<32)-250000, 1000000, 500000, 25);
    check((3ULL<<32)+100, (2ULL<<32)+10, 1000000, 1000000, 50);
    check(100, 10, 120000000, 60000000, 25); // numerator would overflow 32-bit.
    check(100, 10, 3000000000U, 600000000, 10); // denominator would overflow 32-bit.
    auto sample = ThreadProfiler::sample_tasks();
    assert(!ThreadProfiler::take_snapshot(*sample, *sample));
    std::cout << "PASS actual profiler sample/snapshot at " << TEST_BITS
              << " bits: pointer type, wrap, long-uptime and percentage arithmetic\n";
}
""".replace("DECLARATIONS", "\n".join(declarations)).replace("METHODS", methods)
for bits in (32, 64):
    cpp = OUT / f"check-{bits}.cpp"
    cpp.write_text(body)
    exe = OUT / f"check-{bits}.exe"
    p = subprocess.run([str(cc), "/nologo", "/std:c++17", "/EHsc", f"/DTEST_BITS={bits}",
        str(cpp), "/Fe" + str(exe)], env=env, cwd=OUT, capture_output=True, text=True, errors="replace")
    assert p.returncode == 0, (p.stdout, p.stderr)
    subprocess.run([str(exe)], check=True)
# Original sample method must still reproduce the reported pointer type error.
old_text = original.decode()
old_start = old_text.index("std::shared_ptr<ThreadProfiler::SampleResult> ThreadProfiler::sample_tasks()")
old_end = old_text.index("std::shared_ptr<ThreadProfiler::ProfileSnapshot> ThreadProfiler::take_snapshot(", old_start)
new_end = methods.index("std::shared_ptr<ThreadProfiler::ProfileSnapshot> ThreadProfiler::take_snapshot(")
negative = body.replace(methods[:new_end], old_text[old_start:old_end])
(OUT / "negative.cpp").write_text(negative)
p = subprocess.run([str(cc), "/nologo", "/std:c++17", "/EHsc", "/DTEST_BITS=64", "/c",
    str(OUT / "negative.cpp")], env=env, cwd=OUT, capture_output=True, text=True, errors="replace")
assert p.returncode != 0 and "C2664" in p.stdout, (p.stdout, p.stderr)
print("PASS real CMake/flags and SDK-drift guards; reference untouched; original 64-bit pointer error reproduced")
