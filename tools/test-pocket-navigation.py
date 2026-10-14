"""Run the actual LVGL mount/unmount methods with a small native backend double.

This checks screen visibility across Stack reordering, not display rendering.
Also runs the product's C++ edge-gesture policy on boundary cases. The original
reference implementation must fail the paused-screen regression.
"""
from pathlib import Path
import os
import subprocess

root = Path(__file__).resolve().parents[1]
output = root / 'examples/brookesia-minimal/build/host-navigation'
output.mkdir(parents=True, exist_ok=True)
vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
installation = subprocess.check_output([
    str(vswhere), '-latest', '-products', '*', '-requires',
    'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'
], encoding='utf-8').strip()
if not installation:
    raise SystemExit('MSVC C++ tools are required for this host check')
vcvars = Path(installation) / 'VC/Auxiliary/Build/vcvars64.bat'
environment_script = output / 'compiler-environment.cmd'
environment_script.write_text(
    f'@echo off\ncall "{vcvars}" >nul\nif errorlevel 1 exit /b 1\nset\n', encoding='mbcs')
env_output = subprocess.check_output(
    ['cmd.exe', '/d', '/c', str(environment_script)], encoding='mbcs')
environment = dict(os.environ)
for line in env_output.splitlines():
    if '=' in line and not line.startswith('='):
        name, value = line.split('=', 1)
        environment[name] = value
compiler = Path(environment['VCToolsInstallDir']) / 'bin/Hostx64/x64/cl.exe'

prefix = r'''
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include "private/navigation_gesture.hpp"
#define BROOKESIA_LOG_TRACE_GUARD_WITH_THIS() ((void)0)
#define BROOKESIA_LOGD(...) ((void)0)
#define BROOKESIA_LOGE(...) ((void)0)
struct Object { bool hidden=false; Object *parent=nullptr; };
enum { LV_OBJ_FLAG_HIDDEN=1 };
void lv_obj_set_parent(Object *object, Object *parent) { object->parent=parent; }
void lv_obj_remove_flag(Object *object, int) { object->hidden=false; }
void lv_obj_add_flag(Object *object, int) { object->hidden=true; }
void lv_obj_move_foreground(Object *) {}
void lv_obj_update_layout(Object *) {}
bool lv_obj_is_valid(Object *) { return true; }
struct ThreadLockGuard {};
struct BackendHandle {
    using Value=int;
    int id;
    explicit BackendHandle(int value=0):id(value) {}
    int value() const { return id; }
};
struct MountTarget { std::string display_id; int layer=0, mount_mode=0, z_order=0; };
enum class PlacementApplyMask { All };
struct Record {
    struct FrameViewPayload { int props=0; };
    bool hidden=false, is_top_level_screen=true;
    Object *object=nullptr;
    size_t mount_refresh_subtree_count=0;
    template<typename T> T *get_type_payload() { return nullptr; }
};
struct BackendImpl {
    Object parent;
    std::unordered_map<int, Record> records;
    std::unordered_map<int, MountTarget> mounted_targets;
    Record *find_record(BackendHandle h) {
        auto it=records.find(h.value()); return it==records.end()?nullptr:&it->second;
    }
    std::string resolve_display_id(const std::string &id) { return id; }
    Object *resolve_display(const std::string &) { return &parent; }
    Object *resolve_layer_parent(Object *display, int) { return display; }
    void collect_mount_refresh_handles(BackendHandle, std::vector<int> &) {}
    bool mount_screen(BackendHandle, const MountTarget &);
    bool unmount_screen(BackendHandle);
};
bool requires_mount_placement_reapply(const Record &) { return false; }
namespace lvgl {
int get_record_placement(const Record &) { return 0; }
void apply_placement(BackendImpl &, Record &, int, PlacementApplyMask, bool) {}
}
void refresh_frame_view(BackendImpl &, Record &, int) {}
void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
'''
suffix = r'''
int main() {
    try {
        BackendImpl backend;
        Object paused, visible;
        backend.records.emplace(1, Record{true, true, &paused});
        backend.records.emplace(2, Record{false, true, &visible});
        MountTarget target;
        for (int i=0; i<5; ++i) {
            require(backend.mount_screen(BackendHandle(1), target), "Paused mount failed");
            require(backend.mount_screen(BackendHandle(2), target), "Foreground mount failed");
            require(paused.hidden, "Stack reordering exposed a paused screen");
            require(!visible.hidden, "Foreground screen disappeared");
        }
        backend.records.at(1).hidden=false;
        require(backend.mount_screen(BackendHandle(1), target) && !paused.hidden, "Resume failed");
        require(backend.unmount_screen(BackendHandle(1)) && paused.hidden, "Unmount failed");
        require(backend.mount_screen(BackendHandle(1), target) && !paused.hidden, "Remount failed");
        require(!backend.mount_screen(BackendHandle(999), target), "Unknown handle accepted");
        namespace nav=esp_brookesia::system::super::navigation;
        require(nav::back_swipe(24,200,90,210,320,480,1), "Left edge back rejected");
        require(nav::back_swipe(296,200,225,195,320,480,1), "Right edge back rejected");
        require(!nav::back_swipe(60,200,140,205,320,480,1), "Content swipe became back");
        require(!nav::back_swipe(10,10,90,15,320,480,1), "Top gesture became back");
        require(!nav::back_swipe(10,470,90,465,320,480,1), "Bottom gesture became back");
        require(!nav::back_swipe(20,200,90,340,320,480,1), "Vertical scroll became back");
        require(!nav::back_swipe(20,200,40,200,320,480,1), "Short movement became back");
        require(nav::back_swipe(38,200,110,210,320,480,1.25f), "Scaled edge rejected");
        require(nav::back_swipe(775,200,700,210,800,480,1), "Landscape edge rejected");
        std::cout << "PASS: paused-screen reorder, resume, unmount/remount, edge gesture boundaries\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
'''

def run(component):
    backend_source = (component / 'src/backend.cpp').read_text(encoding='utf-8')
    begin = backend_source.index('bool BackendImpl::mount_screen(')
    end = backend_source.index('bool BackendImpl::register_display(', begin)
    source = output / 'navigation.cpp'
    source.write_text(prefix + backend_source[begin:end] + suffix, encoding='utf-8')
    exe = output / 'navigation.exe'
    compiled = subprocess.run([
        str(compiler), '/nologo', '/std:c++20', '/EHsc', '/utf-8',
        '/I' + str(root / 'components/brookesia_system_super/src'),
        str(source), '/Fe:' + str(exe), '/Fo:' + str(output / 'navigation.obj')
    ], env=environment, cwd=output, capture_output=True)
    if compiled.returncode:
        print(compiled.stdout.decode('mbcs', errors='replace'))
        print(compiled.stderr.decode('mbcs', errors='replace'))
        raise SystemExit(compiled.returncode)
    return subprocess.run([str(exe)], cwd=output, capture_output=True, text=True)

before = run(root / '.reference/esp-brookesia/gui/brookesia_gui_lvgl')
assert before.returncode != 0 and 'exposed a paused screen' in before.stderr, before
print('PASS: reference backend reproduces paused-screen visibility failure')
after = run(root / 'components/brookesia_gui_lvgl')
print(after.stdout, end='')
if after.returncode:
    raise SystemExit(after.stderr)
