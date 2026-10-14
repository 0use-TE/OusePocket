"""Headless integration check with the project's actual LVGL 9.5 software renderer.

Compile the real DesktopImpl methods, substituting only BackendImpl's registry
and event routing. Verify screenshot pixels, restoration of the source scene,
static-preview retention, disposal, light/dark UI, and gesture classification.
Does not test the ESP display/touch port or its frame rate.
"""
from pathlib import Path
import os
import subprocess
import sys
sys.stdout.reconfigure(errors='replace')

root = Path(__file__).resolve().parents[1]
output = root / 'examples/brookesia-minimal/build/host-recents'
output.mkdir(parents=True, exist_ok=True)
vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
installation = subprocess.check_output([str(vswhere), '-latest', '-products', '*', '-requires',
    'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], encoding='utf-8').strip()
vcvars = Path(installation) / 'VC/Auxiliary/Build/vcvars64.bat'
script = output / 'compiler-environment.cmd'
script.write_text(f'@echo off\ncall "{vcvars}" >nul\nif errorlevel 1 exit /b 1\nset\n', encoding='mbcs')
environment = dict(os.environ)
for line in subprocess.check_output(['cmd.exe', '/d', '/c', str(script)], encoding='mbcs').splitlines():
    if '=' in line and not line.startswith('='):
        name, value = line.split('=', 1)
        environment[name] = value

prefix = r'''
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <expected>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include "lvgl.h"
#include "private/recent_gesture.hpp"
namespace esp_brookesia::lib_utils {
struct FunctionGuard { std::function<void()> f; explicit FunctionGuard(std::function<void()> fn):f(fn) {} ~FunctionGuard(){f();} };
}
namespace esp_brookesia::gui::lvgl {
struct BackendHandle { int id=0; bool operator==(const BackendHandle&) const=default; };
enum class EventType { Clicked };
struct Value {
    std::string text; uint64_t number=0;
    Value(std::string_view v):text(v) {}
    Value(uint32_t v):number(v) {}
    Value(uint64_t v):number(v) {}
};
struct BackendEvent { BackendHandle handle; EventType type; std::string action; std::map<std::string,Value> payload; };
struct RecentTask { uint32_t app_id; std::string name; std::string icon_path; };
struct BinaryImageSource { lv_image_dsc_t descriptor {}; };
struct Record { BackendHandle handle; std::string path; bool is_top_level_screen=true; lv_obj_t *object=nullptr; bool hidden=false; };
struct BackendImpl {
    std::map<int,Record> records; std::map<int,int> mounted_targets;
    std::function<void(const BackendEvent&)> event_sink;
    lv_display_t *default_display() { return lv_display_get_default(); }
    std::string build_absolute_path(const Record &r) { return r.path; }
    Record *find_record_by_absolute_path(std::string_view path) {
        for(auto &[id,record]:records) if(record.path==path) return &record;
        return nullptr;
    }
};
void lock_thread() {} void unlock_thread() {}
std::expected<std::shared_ptr<BinaryImageSource>,std::string> load_image_source(std::string_view) {
    return std::unexpected("No external icon in this check");
}
}
'''
header = (root / 'components/brookesia_gui_lvgl/src/private/pocket_desktop.hpp').read_text(encoding='utf-8')
header = '\n'.join(line for line in header.splitlines() if not line.startswith(('#include', '#pragma')))
# Inspection access is test-only; the methods under test are copied unchanged.
header = header.replace('private:', 'public:')
implementation = (root / 'components/brookesia_gui_lvgl/src/pocket_desktop.cpp').read_text(encoding='utf-8')
implementation = implementation[:implementation.index('std::expected<void, std::string> Backend::capture_app_snapshot')]
implementation = '\n'.join(line for line in implementation.splitlines() if not line.startswith('#include')) + '\n}\n'
suffix = r'''
void require(bool condition,const char *message) { if(!condition) throw std::runtime_error(message); }
uint16_t pixel(lv_draw_buf_t *buffer,int x,int y) {
    return reinterpret_cast<uint16_t *>(buffer->data+y*buffer->header.stride)[x];
}
lv_obj_t *rectangle(lv_obj_t *parent,int x,int y,int w,int h,uint32_t color) {
    auto *object=lv_obj_create(parent); lv_obj_remove_style_all(object); lv_obj_set_pos(object,x,y); lv_obj_set_size(object,w,h);
    lv_obj_set_style_bg_color(object,lv_color_hex(color),0); lv_obj_set_style_bg_opa(object,LV_OPA_COVER,0); return object;
}
int main() {
    try {
        lv_init(); auto *display=lv_display_create(320,480);
        static uint8_t display_buffer[320*40*2];
        lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
        lv_display_set_buffers(display,display_buffer,nullptr,sizeof(display_buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);
        lv_display_set_flush_cb(display,[](lv_display_t *d,const lv_area_t *,uint8_t *) { lv_display_flush_ready(d); });
        namespace g=esp_brookesia::gui::lvgl;
        g::BackendImpl backend;
        auto *screen=lv_screen_active(); auto *top=lv_layer_top();
        auto *page=rectangle(screen,0,0,320,480,0xff0000);
        auto *header=rectangle(top,0,0,320,40,0x00ff00);
        auto *system=rectangle(top,0,0,320,40,0x0000ff); // must not leak into app screenshot
        backend.records.emplace(1,g::Record{{1},"/page",true,page,false}); backend.mounted_targets[1]=1;
        backend.records.emplace(2,g::Record{{2},"/header",true,header,false}); backend.mounted_targets[2]=1;
        backend.records.emplace(3,g::Record{{3},"/overlay",true,system,false}); backend.mounted_targets[3]=1;
        lv_obj_update_layout(screen); lv_obj_update_layout(top);
        const int header_index=lv_obj_get_index(header), page_index=lv_obj_get_index(page);
        g::DesktopImpl desktop(backend);
        auto captured=desktop.capture(2,{"/page","/header"},false);
        require(captured.has_value(),"Screenshot failed");
        auto image=desktop.snapshots_.at(2); auto *original=image->buffer;
        require(original->header.w==320 && original->header.h==480,"Wrong snapshot resolution");
        require(pixel(original,10,100)==0xf800,"App content missing from screenshot");
        require(pixel(original,10,10)==0x07e0,"Header missing or Shell overlay leaked into screenshot");
        require(lv_obj_get_parent(header)==top && lv_obj_get_index(header)==header_index,"Header parent/order not restored");
        require(lv_obj_get_parent(page)==screen && lv_obj_get_index(page)==page_index,"Page parent/order not restored");
        lv_obj_set_style_bg_color(page,lv_color_hex(0xffffff),0);
        require(pixel(original,10,100)==0xf800,"Preview is live instead of a static screenshot");
        require(desktop.capture(2,{"/page","/header"},false).has_value(),"Snapshot update failed");
        require(desktop.snapshots_.at(2)->buffer==original,"Same-sized snapshot allocation was not reused");
        require(pixel(original,10,100)==0xffff,"Updated screenshot is stale");
        require(!desktop.capture(7,{"/missing"},false),"Missing screen accepted");
        lv_obj_add_flag(page,LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(header,LV_OBJ_FLAG_HIDDEN);
        require(!desktop.capture(7,{"/page","/header"},false),"Paused screens were captured");
        auto *host=rectangle(top,0,0,320,480,0xffffff); auto *title=lv_label_create(host); lv_label_set_text(title,"Recent apps");
        backend.records.emplace(4,g::Record{{4},"/overlay/recents",false,host,false});
        backend.records.emplace(5,g::Record{{5},"/overlay/recents/title",false,title,false});
        require(desktop.show({{2,"Settings",""}},2,false,false,1,false).has_value(),"Light recent task view failed");
        require(desktop.cards_.size()==1 && desktop.cards_[0]->snapshot==image,"Screenshot card did not retain image");
        lv_obj_update_layout(host);
        require(lv_obj_get_width(desktop.cards_[0]->slot)==180,"Card width differs from Phone profile");
        require(lv_obj_get_height(desktop.cards_[0]->slot)==302,"Card height differs from Phone profile");
        // Rendering also checks image descriptor lifetime and rounded clipping.
        auto *view=lv_snapshot_take(host,LV_COLOR_FORMAT_RGB565); require(view!=nullptr,"Recent task rendering failed"); lv_draw_buf_destroy(view);
        desktop.release(2); require(!desktop.snapshots_.contains(2),"STOP retained cached screenshot");
        require(desktop.cards_[0]->snapshot->buffer==original,"Card has dangling snapshot after STOP");
        desktop.hide(); require(desktop.cards_.empty(),"Recent task cards not cleaned");
        require(desktop.show({},0,true,false,2,false).has_value(),"Dark empty task list failed");
        require(lv_color_to_u32(lv_obj_get_style_bg_color(host,LV_PART_MAIN))==lv_color_to_u32(lv_color_hex(0x1a1a1a)),"Dark theme color incorrect");
        desktop.back_hint(0,220,60,48,false); require(!lv_obj_has_flag(desktop.hint_,LV_OBJ_FLAG_HIDDEN),"Back hint missing");
        desktop.hide_back_hint(); require(lv_obj_has_flag(desktop.hint_,LV_OBJ_FLAG_HIDDEN),"Back hint stuck visible");
        desktop.navigation_edges(32,32); require(lv_obj_get_width(desktop.left_edge_)==32,"Edge guard width incorrect");
        namespace gesture=g::recent;
        require(gesture::choose_axis(3,4,5)==gesture::Axis::None,"Tap became drag");
        require(gesture::choose_axis(6,-40,5)==gesture::Axis::Vertical,"Vertical drag became horizontal");
        require(gesture::release(gesture::Axis::Vertical,0,-31,30)==gesture::Action::Close,"Upward drag did not close");
        require(gesture::release(gesture::Axis::Vertical,0,-15,30)==gesture::Action::Rebound,"Short drag closed app");
        require(gesture::release(gesture::Axis::Horizontal,-50,-40,30)==gesture::Action::Next,"Horizontal navigation closed app");
        // Exercise real pointer dispatch, including the native host event route.
        desktop.hide_back_hint();
        lv_obj_add_flag(desktop.left_edge_,LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(desktop.right_edge_,LV_OBJ_FLAG_HIDDEN);
        std::vector<g::BackendEvent> events;
        backend.event_sink=[&](const g::BackendEvent &event) { events.push_back(event); };
        static lv_point_t pointer{}; static lv_indev_state_t state=LV_INDEV_STATE_RELEASED;
        auto *input=lv_indev_create(); lv_indev_set_type(input,LV_INDEV_TYPE_POINTER); lv_indev_set_display(input,display);
        lv_indev_set_read_cb(input,[](lv_indev_t*,lv_indev_data_t *data) { data->point=pointer; data->state=state; });
        auto send=[&](int x,int y,bool pressed) { pointer={static_cast<int16_t>(x),static_cast<int16_t>(y)};
            state=pressed?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED; lv_tick_inc(20); lv_indev_read(input); };
        require(desktop.show({{2,"Settings",""},{3,"Player",""}},2,false,false,9,true).has_value(),"Input task view failed");
        send(160,200,true); send(160,200,false);
        require(events.size()==1 && events.back().payload.at("operation").text=="open","Card tap did not resume");
        require(events.back().handle.id==4 && events.back().payload.at("generation").number==9,"Host routing identity lost");
        events.clear(); send(160,200,true); send(110,200,true); send(110,200,false);
        require(events.empty() && desktop.focus_==1,"Horizontal pointer swipe closed/resumed app instead of changing card");
        lv_tick_inc(400); lv_timer_handler();
        send(160,200,true); send(160,155,true); send(160,155,false);
        require(events.size()==1 && events.back().payload.at("operation").text=="close" &&
            events.back().payload.at("app_id").number==3,"Upward pointer swipe did not close selected app");
        events.clear(); send(160,200,true); send(160,190,true); send(160,190,false);
        require(events.empty(),"Short upward swipe closed app");
        lv_area_t button_area{}; lv_obj_get_coords(lv_obj_get_child(desktop.body_,-1),&button_area);
        const int button_y=(button_area.y1+button_area.y2)/2;
        send(160,button_y,true); send(160,button_y,false);
        require(events.size()==1 && events.back().payload.at("operation").text=="close_all","Close all pointer button failed");
        lv_indev_delete(input);
        desktop.hide(); desktop.clear();
        std::cout<<"PASS: real LVGL screenshot pixels, scene restoration, static preview/reuse, card rendering/lifetime, light/dark, back hint, pointer gestures and host events\n";
    } catch(const std::exception &error) { std::cerr<<error.what()<<'\n'; return 1; }
}
'''
(output / 'recents.cpp').write_text(prefix + header + implementation + suffix, encoding='utf-8')
lvgl = root / 'examples/brookesia-minimal/managed_components/lvgl__lvgl'
cmake = Path('D:/Espressif/tools/cmake/4.0.3/bin/cmake.exe')
ninja = Path('D:/Espressif/tools/ninja/1.12.1/ninja.exe')
(output / 'CMakeLists.txt').write_text(f'''
cmake_minimum_required(VERSION 3.16)
project(recents_check C CXX)
file(GLOB_RECURSE lvgl_sources "{lvgl.as_posix()}/src/*.c")
add_library(lvgl STATIC ${{lvgl_sources}})
target_include_directories(lvgl PUBLIC "{lvgl.as_posix()}")
target_compile_definitions(lvgl PUBLIC LV_CONF_SKIP=1 LV_USE_SNAPSHOT=1 LV_USE_STDLIB_MALLOC=1)
add_executable(recents_check recents.cpp)
target_compile_options(recents_check PRIVATE /std:c++latest /utf-8 /EHsc)
target_include_directories(recents_check PRIVATE "{(root / 'components/brookesia_gui_lvgl/src').as_posix()}")
target_link_libraries(recents_check PRIVATE lvgl)
''', encoding='utf-8')
def run(args):
    result = subprocess.run(args, cwd=output, env=environment, capture_output=True)
    if result.returncode:
        (output / 'failure.log').write_bytes(result.stdout + result.stderr)
        lines = result.stdout.decode('utf-8',errors='replace').splitlines()
        useful = [line for line in lines if 'error ' in line or 'FAILED:' in line or 'PASS:' in line]
        print('\n'.join(useful) if useful else '\n'.join(lines[-15:]))
        print(result.stderr.decode('mbcs',errors='replace')[-3000:])
        raise SystemExit(result.returncode)
    return result
run([str(cmake),'-G','Ninja','-S',str(output),'-B',str(output/'compiled'),'-DCMAKE_MAKE_PROGRAM='+str(ninja)])
run([str(cmake),'--build',str(output/'compiled'),'-j','8'])
result = run([str(output/'compiled/recents_check.exe')])
print(result.stdout.decode('utf-8'),end='')
