"""Parse product GUI assets with Brookesia's real parser and validator on Windows.

Only logging and Storage's filesystem transport are replaced. This catches
unsupported expressions, invalid layouts/actions, and missing resource IDs.
Visual fixtures use real LVGL layout/drawing with a small document adapter and
TinyTTF. They check geometry and assets, not the full ESP runtime/FreeType port.
"""
import os
import shutil
import subprocess
import sys
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'examples/brookesia-minimal/build/host-shell-controls'
OUT.mkdir(parents=True, exist_ok=True)
sys.stdout.reconfigure(errors='replace')
fixture_python = Path(sys.executable) if importlib.util.find_spec('PIL') else (
    Path.home() / '.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe')
subprocess.run([str(fixture_python), str(ROOT/'tools/make-ui-fixtures.py')],check=True)
SOURCE = ROOT / '.reference/esp-brookesia/gui/brookesia_gui_interface'
BOOST = ROOT / 'examples/brookesia-minimal/managed_components/espressif__esp-boost/src'
vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
vs = subprocess.check_output([str(vswhere), '-latest', '-products', '*', '-requires',
    'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], encoding='utf-8').strip()
cmd = OUT / 'environment.cmd'
cmd.write_text(f'@echo off\ncall "{vs}/VC/Auxiliary/Build/vcvars64.bat" >nul\nset\n', encoding='mbcs')
env = dict(os.environ)
for line in subprocess.check_output(['cmd.exe','/d','/c',str(cmd)], encoding='mbcs').splitlines():
    if '=' in line and not line.startswith('='):
        key, value = line.split('=',1)
        env[key] = value

sources = [*SOURCE.joinpath('src').glob('parser*.cpp'), SOURCE/'src/validator.cpp', SOURCE/'src/binding.cpp']
(OUT/'private').mkdir(exist_ok=True)
for path in sources:
    shutil.copyfile(path, OUT/path.name)
for name in ['parser_impl.hpp','binding.hpp']:
    shutil.copyfile(SOURCE/'src/private'/name, OUT/'private'/name)
(OUT/'private/utils.hpp').write_text('''#pragma once
#define BROOKESIA_LOG_TRACE_GUARD(...) ((void)0)
#define BROOKESIA_LOG_TRACE_GUARD_WITH_THIS(...) ((void)0)
#define BROOKESIA_LOGD(...) ((void)0)
#define BROOKESIA_LOGI(...) ((void)0)
#define BROOKESIA_LOGW(...) ((void)0)
#define BROOKESIA_LOGE(...) ((void)0)
''')
storage = OUT/'brookesia/service_helper/system/storage.hpp'
storage.parent.mkdir(parents=True, exist_ok=True)
storage.write_text('''#pragma once
#include <fstream>
#include <expected>
#include <string>
#include <iterator>
namespace esp_brookesia::service::helper {
struct Storage {
 static std::expected<std::string,std::string> fs_read_text(const std::string &path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) return std::unexpected("File unavailable: " + path);
  return std::string(std::istreambuf_iterator<char>(stream), {});
 }
}; }
''')
main = OUT/'main.cpp'
def method(path, signature):
    text = path.read_text(encoding='utf-8')
    start = text.index(signature)
    opening = text.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

shell = ROOT/'components/brookesia_system_super/src'
touch = (shell/'shell_display.cpp').read_text(encoding='utf-8')
quick_branch = method(shell/'shell_display.cpp', '    if (quick_controls_open_) {')
page_start = touch.index('    if (foreground_is_shell_ && !started_from_top && !started_from_bottom)')
page_branch = method(shell/'shell_display.cpp', touch[page_start:touch.index('{',page_start)].strip())
gesture_test = r'''
#include <algorithm>
#include <functional>
#include <map>
#include <stdexcept>
#include <cmath>
namespace controls_check {
namespace gui = esp_brookesia::gui;
constexpr auto SUPER_LAUNCHER_ITEM_LAYER_PATH = "/launcher/content/grid_stage/item_layer";
struct FakeGui {
 std::map<std::string,std::string> bindings;
 std::function<void()> completed;
 bool fail=false;
 struct Result { unsigned subscription_id=1; };
 std::expected<void,std::string> set_binding_value(std::string_view p,std::string_view k,std::string_view v) {
  bindings[std::string(p)+":"+std::string(k)]=v; return {};
 }
 std::expected<Result,std::string> start_view_animation_with_result(std::string_view,const gui::Animation&,std::function<void()> cb) {
  if(fail) return std::unexpected("Injected animation failure"); completed=cb; return Result{};
 }
 void stop_animation(unsigned) { completed={}; }
 void finish() { auto f=completed; completed={}; if(f) f(); }
};
struct Context { FakeGui gui_; FakeGui &gui() { return gui_; } };
struct Owner { gui::Environment get_environment() { return {}; } };
struct DisplayService {
 enum class TouchGestureEventType { Press,Pressing,Release };
 struct TouchGestureInfo { TouchGestureEventType event_type; int start_x=160,start_y=0,stop_x=160,stop_y=0; };
};
gui::Animation make_timed_animation(gui::AnimationProperty property,int to,int duration) {
 gui::Animation a; a.property=property; a.to=to; a.duration=duration; return a;
}
struct ShellApp {
 Owner owner_; Context context; Context *context_=&context;
 bool quick_controls_open_=true,quick_controls_dragging_=true,quick_controls_closing_=false,quick_controls_ready_=false;
 int quick_controls_y_=-480; unsigned quick_controls_animation_id_=0; uint64_t status_peek_generation_=0;
 int launcher_page_=0,launcher_pages_=3,launcher_page_width_=242;
 bool launcher_swiping_=false,launcher_ignore_click_=false;
 unsigned launcher_page_animation_id_=0; uint64_t launcher_page_generation_=0;
 void close_quick_controls() { ++status_peek_generation_; quick_controls_open_=false; quick_controls_ready_=false; }
 void move_quick_controls(int32_t); void settle_quick_controls(bool);
 void move_launcher_page(int32_t); void settle_launcher_page(int32_t);
 void quick_touch(const DisplayService::TouchGestureInfo &info,bool edge_back=false) {
  auto environment=owner_.get_environment();
  QUICK_BRANCH
 }
 void page_touch(const DisplayService::TouchGestureInfo &info,bool started_from_top=false,bool started_from_bottom=false) {
  bool foreground_is_shell_=true; auto environment=owner_.get_environment();
  PAGE_BRANCH
 }
};
METHODS
void require(bool condition,const char *why) { if(!condition) throw std::runtime_error(why); }
void test() {
 using E=DisplayService::TouchGestureEventType;
 ShellApp shell;
 shell.quick_touch({E::Pressing,160,0,160,180});
 require(shell.quick_controls_y_==-300 && !shell.quick_controls_ready_,"Opening must follow finger with controls locked");
 shell.quick_touch({E::Release,160,0,160,180});
 require(!shell.quick_controls_ready_,"Release must not enable controls before settlement");
 shell.context.gui().finish();
 require(shell.quick_controls_ready_ && shell.quick_controls_y_==0,"Expanded sheet must become interactive");
 shell.quick_touch({E::Press,160,280,160,280});
 shell.quick_touch({E::Pressing,160,280,160,180});
 require(!shell.quick_controls_dragging_ && shell.quick_controls_y_==0,"Slider drag must not dismiss sheet");
 shell.quick_touch({E::Press,160,470,160,470});
 shell.quick_touch({E::Pressing,160,470,160,220});
 require(shell.quick_controls_y_==-250 && !shell.quick_controls_ready_,"Handle dismissal must follow finger and lock input");
 shell.quick_touch({E::Release,160,470,160,220}); shell.context.gui().finish();
 require(!shell.quick_controls_open_,"Long upward handle drag must dismiss");
 ShellApp short_pull;
 short_pull.quick_touch({E::Release,160,0,160,30}); short_pull.context.gui().finish();
 require(!short_pull.quick_controls_open_,"Short pull must cancel opening");
 ShellApp failure; failure.context.gui().fail=true; failure.settle_quick_controls(true);
 require(failure.quick_controls_ready_,"Animation failure must not leave input permanently locked");
 ShellApp stale; stale.settle_quick_controls(true); stale.close_quick_controls(); stale.context.gui().finish();
 require(!stale.quick_controls_ready_,"Old animation completion must not revive closed sheet");
 ShellApp pages;
 pages.page_touch({E::Press,160,180,160,180});
 pages.page_touch({E::Pressing,160,180,60,180});
 require(pages.launcher_swiping_ && pages.launcher_ignore_click_,"Page dragging must suppress app clicks");
 pages.page_touch({E::Release,160,180,60,180}); pages.context.gui().finish();
 require(pages.launcher_page_==1,"Left swipe must select next page");
 pages.settle_launcher_page(99); pages.context.gui().finish();
 require(pages.launcher_page_==2,"Last page must clamp");
 pages.settle_launcher_page(-99); pages.context.gui().finish();
 require(pages.launcher_page_==0,"First page must clamp");
 pages.launcher_pages_=1;
 pages.page_touch({E::Press,160,180,160,180});
 pages.page_touch({E::Pressing,160,180,160,280});
 require(!pages.launcher_swiping_ && pages.launcher_ignore_click_,"Single page must stay fixed and ignore dragging clicks");
 pages.page_touch({E::Press,160,0,160,0},true);
 require(pages.launcher_ignore_click_,"Top-edge pull must bypass pagination");
 std::cout << "PASS: actual Shell gesture branches, follow/settle/input lock, drag ownership, animation failure, stale callback, page boundaries\n";
}
}
'''
methods = '\n'.join(method(shell/name, signature) for name,signature in [
    ('shell_quick_controls.cpp','void ShellApp::move_quick_controls('),
    ('shell_quick_controls.cpp','void ShellApp::settle_quick_controls('),
    ('shell_app_launcher.cpp','void ShellApp::move_launcher_page('),
    ('shell_app_launcher.cpp','void ShellApp::settle_launcher_page(')])
gesture_test=gesture_test.replace('QUICK_BRANCH',quick_branch).replace('PAGE_BRANCH',page_branch).replace('METHODS',methods)

preference_test = r'''
#include <optional>
#include <memory>
#define BROOKESIA_LOGW(...) ((void)0)
namespace preference_check {
namespace core {
 enum class MessageDialogIcon { Question, Warning };
 enum class MessageDialogButtonRole { Accept, Reject };
 enum class MessageDialogCloseReason { Button, Closed };
 struct Button { std::string text; MessageDialogButtonRole role; };
 struct MessageDialogOptions { std::string text,informative_text; MessageDialogIcon icon; std::vector<Button> buttons; };
 struct MessageDialogResult { MessageDialogCloseReason reason; int button_index; };
}
namespace hal {
 namespace system { struct GeneralIface { std::expected<void,std::string> restart(); }; }
 int restarts=0; bool unavailable=false,fail_restart=false;
 std::expected<void,std::string> system::GeneralIface::restart() {
  ++restarts; if(fail_restart) return std::unexpected("Injected restart failure"); return {};
 }
 template<class T> std::shared_ptr<T> acquire_first_interface() {
  return unavailable?nullptr:std::make_shared<T>();
 }
}
struct Gui {
 std::string active="light",language="zh_CN";
 std::optional<std::string> stored;
 bool fail_save=false; std::string feedback;
 std::string get_theme() { return active; }
 std::string get_language() { return language; }
 auto get_stored_theme_preference() { return stored; }
 std::expected<void,std::string> save_theme_preference(std::string v) {
  if(fail_save) return std::unexpected("Injected save failure"); stored=v; return {};
 }
 void set_binding_value(const std::string&,const std::string&,const std::string& v) { feedback=v; }
};
struct Context { Gui gui_; Gui &gui() { return gui_; } };
struct Owner {
 int prompts=0; core::MessageDialogOptions options;
 std::function<void(const core::MessageDialogResult&)> answer;
 std::expected<unsigned,std::string> show_system_message_dialog(core::MessageDialogOptions o,
   std::function<void(const core::MessageDialogResult&)> f={}) {
  ++prompts; options=std::move(o); answer=std::move(f); return prompts;
 }
 void respond(core::MessageDialogCloseReason reason,int index) {
  auto f=std::move(answer); if(f) f({reason,index});
 }
};
constexpr auto PANEL="/panel";
struct Shell {
 Context context; Context *context_=&context; Owner owner_; bool closed=false;
 void refresh_quick_controls() {}
 void close_quick_controls() { closed=true; }
 void handle() { struct { std::string action="ouse.quick.theme"; } event;
 THEME_BRANCH
 }
};
BATTERY_METHOD
void test() {
 auto require=[](bool v,const char *why){if(!v) throw std::runtime_error(why);};
 for(bool dark:{false,true}) for(int p=0;p<=100;++p) {
  const int expected=p==0?0:1+(p-1)/20;
  const std::string suffix=dark?".dark":".light";
  require(battery_image_id(p,false,dark)=="battery.level"+std::to_string(expected)+suffix,"Wrong 20% battery bucket/theme");
  require(battery_image_id(p,true,dark)=="battery.charging"+std::to_string(expected)+suffix,"Wrong charging bucket/theme");
 }
 require(battery_image_id(-1,false,false)=="battery.unknown.light","Unknown power shown as zero");
 require(battery_image_id(101,false,true)=="battery.level5.dark","Battery not clamped at full");
 Shell later; later.handle();
 require(later.context.gui_.stored=="dark" && later.context.gui_.active=="light","Theme must save without reapplying documents");
 require(later.closed && later.owner_.prompts==1 && later.owner_.options.buttons.size()==2,"Missing restart/later prompt");
 later.owner_.respond(core::MessageDialogCloseReason::Button,1);
 require(hal::restarts==0 && later.context.gui_.stored=="dark","Later restarted or lost preference");
 later.handle(); require(later.context.gui_.stored=="light" && later.owner_.prompts==1,"Second toggle must cancel pending theme");
 Shell now; now.handle(); now.owner_.respond(core::MessageDialogCloseReason::Button,0);
 require(hal::restarts==1,"Restart now did not use the HAL");
 Shell dismissed; dismissed.handle(); dismissed.owner_.respond(core::MessageDialogCloseReason::Closed,-1);
 require(hal::restarts==1,"Dismissed prompt restarted device");
 Shell failed; failed.context.gui_.fail_save=true; failed.handle();
 require(failed.owner_.prompts==0 && !failed.closed && !failed.context.gui_.feedback.empty(),"Save failure silently closed controls");
 hal::unavailable=true; Shell no_hal; no_hal.handle(); no_hal.owner_.respond(core::MessageDialogCloseReason::Button,0);
 require(no_hal.owner_.prompts==2 && hal::restarts==1,"Missing HAL did not report restart failure"); hal::unavailable=false;
 hal::fail_restart=true; Shell broken; broken.handle(); broken.owner_.respond(core::MessageDialogCloseReason::Button,0);
 require(broken.owner_.prompts==2,"HAL failure did not report restart failure"); hal::fail_restart=false;
 std::cout<<"PASS: actual theme action, deferred preference, cancel/later/restart/failure paths and all battery buckets\n";
}
}
'''.replace('THEME_BRANCH',method(shell/'shell_quick_controls.cpp','    if (event.action == "ouse.quick.theme") {')) \
 .replace('BATTERY_METHOD',method(shell/'shell_status.cpp','std::string battery_image_id('))
main.write_text(r'''
#include "boost/json/src.hpp"
#include "brookesia/gui_interface/parser.hpp"
#include "brookesia/gui_interface/validator.hpp"
#include <iostream>
#include "ui-layout-check.hpp"
''' + gesture_test + preference_test + r'''
using namespace esp_brookesia::gui;
int main(int argc, char **argv) {
 controls_check::test();
 preference_check::test();
 try {
 for (float density : {0.75f, 1.0f, 1.25f}) for (auto theme : {"light","dark"}) for(auto locale:{"en","zh_CN"}) {
  Environment environment{.width_px=320,.height_px=480,.density=density,.language=locale,.theme_id=theme};
  auto tokens=parse_theme_asset_file(std::string(argv[2])+"/components/brookesia_system_super/resource/themes/"+theme+".json",environment);
  if(!tokens) throw std::runtime_error(tokens.error());
  environment.colors=tokens->colors;
  for(bool shell:{true,false}) {
   auto path=shell?std::string(argv[1]):std::string(argv[2])+"/components/brookesia_app_settings/package/res/root.json";
   auto result = parse_document_file(path, environment);
   if (!result) throw std::runtime_error(result.error());
   auto validation = validate_document(*result);
   for (const auto &error : validation.errors) std::cerr << error << '\n';
   if (!validation.errors.empty()) return 2;
   layout_check::check(*result,*tokens,environment,argv[2],argv[3],shell);
  }
  for(auto app:{"music","log-viewer","resource-monitor"}) {
   auto result=parse_document_file(std::string(argv[2])+"/examples/"+app+"/src/res/root.json",environment);
   if(!result) throw std::runtime_error(result.error());
   auto validation=validate_document(*result);
   if(!validation.errors.empty()) throw std::runtime_error(validation.errors.front());
   layout_check::check_app(*result,*tokens,environment,argv[2],argv[3],app);
  }
 }
 } catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 3; }
 std::cout << "PASS: real Brookesia parser/validator and LVGL layout fixtures, both languages/themes, 75/100/125%\n";
}
''', encoding='utf-8')
compiler = Path(env['VCToolsInstallDir'])/'bin/Hostx64/x64/cl.exe'
lvgl = ROOT / 'examples/brookesia-minimal/managed_components/lvgl__lvgl'
cmake = Path('D:/Espressif/tools/cmake/4.0.3/bin/cmake.exe')
ninja = Path('D:/Espressif/tools/ninja/1.12.1/ninja.exe')
(OUT/'CMakeLists.txt').write_text(f'''
cmake_minimum_required(VERSION 3.16)
project(layout_fixture C)
file(GLOB_RECURSE lvgl_sources "{lvgl.as_posix()}/src/*.c")
add_library(lvgl STATIC ${{lvgl_sources}})
target_include_directories(lvgl PUBLIC "{lvgl.as_posix()}")
target_compile_definitions(lvgl PUBLIC LV_CONF_SKIP=1 LV_USE_SNAPSHOT=1 LV_USE_STDLIB_MALLOC=1 LV_USE_TINY_TTF=1 LV_TINY_TTF_FILE_SUPPORT=0)
''',encoding='utf-8')
for args in ([str(cmake),'-G','Ninja','-S',str(OUT),'-B',str(OUT/'compiled'),'-DCMAKE_MAKE_PROGRAM='+str(ninja)],
             [str(cmake),'--build',str(OUT/'compiled'),'-j','8']):
    result=subprocess.run(args,env=env,capture_output=True,text=True,errors='replace')
    if result.returncode:
        print(result.stdout[-5000:]); print(result.stderr[-3000:]); raise SystemExit(result.returncode)
command = [str(compiler),'/nologo','/std:c++latest','/EHsc','/utf-8','/Od','/MDd',
    '/DLV_CONF_SKIP=1','/DLV_USE_SNAPSHOT=1','/DLV_USE_STDLIB_MALLOC=1',
    '/DLV_USE_TINY_TTF=1','/DLV_TINY_TTF_FILE_SUPPORT=0',f'/I{lvgl}',
    '/DBROOKESIA_GUI_INTERFACE_PARSER_ENABLE_DEBUG_LOG=0',
    '/DBROOKESIA_GUI_INTERFACE_VALIDATOR_ENABLE_DEBUG_LOG=0',
    '/DBOOST_ALL_NO_LIB','/DBOOST_NO_USER_CONFIG',
    f'/I{OUT}',f'/I{ROOT / "tools"}',f'/I{SOURCE / "include"}', f'/I{BOOST}',
    f'/I{ROOT / ".reference/esp-brookesia/utils/brookesia_lib_utils/include"}',
    str(main), *[str(OUT/p.name) for p in sources],str(OUT/'compiled/lvgl.lib'), f'/Fe{OUT / "check.exe"}']
result = subprocess.run(command, cwd=OUT, env=env, capture_output=True, text=True, encoding='utf-8',errors='replace')
if result.returncode:
    print(result.stdout[-18000:]); print(result.stderr[-4000:])
    raise SystemExit(result.returncode)
subprocess.run([str(OUT/'check.exe'),str(ROOT/'components/brookesia_system_super/resource/shell/root.json'),str(ROOT),str(OUT)],check=True)
