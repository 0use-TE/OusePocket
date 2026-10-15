"""Run the real preference helper and deferred UI recovery with host-side service I/O."""
from pathlib import Path
import os
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / ".tools/deferred-network-ui-check"
OUT.mkdir(parents=True, exist_ok=True)


def block(source, marker):
    start = source.index(marker)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


settings = (ROOT / "components/brookesia_app_settings/src/service/services.ipp").read_text()
lifecycle = (ROOT / "components/brookesia_app_settings/src/app/lifecycle.ipp").read_text()
shell = (ROOT / "components/brookesia_system_super/src/shell_app.cpp").read_text()
recovery = block(settings, "void SettingsApp::refresh_deferred_network_services(")
apply_preference = block(settings, "void SettingsApp::apply_wifi_enabled_preference_to_service(").replace(
    "SettingsApp::apply_wifi_enabled_preference_to_service", "PreferenceApp::apply_wifi_enabled_preference_to_service"
)
clock = block(shell, "if (name == SUPER_STATUS_CLOCK_TIMER_NAME && timer_id == status_clock_timer_id_)")
assert "read_wifi_enabled_preference(SETTINGS_STORAGE_TIMEOUT_MS)" in block(
    settings, "bool SettingsApp::load_wifi_enabled_preference()"
)
assert "refresh_deferred_network_services(context);" in block(lifecycle, 'if (name == "ouse.power.poll")')
assert "refresh_deferred_network_services(context);" in block(
    lifecycle, 'if (effective_action == "settings.open.wifi")'
)
toggle = block(lifecycle, "if (effective_action == WIFI_TOGGLE_ACTION)")
assert "return disconnect_wifi(context);" in toggle
assert "apply_wifi_enabled_preference_to_service(true);" in toggle
wifi = (ROOT / "components/brookesia_app_settings/src/service/wifi.ipp").read_text()
assert "BROOKESIA_DESCRIBE_TO_STR(WifiHelper::GeneralAction::Stop)" in block(
    wifi, "std::expected<void, std::string> SettingsApp::disconnect_wifi("
)
assert "apply_wifi_enabled_preference_to_service(true);" in block(
    wifi, "std::expected<void, std::string> SettingsApp::disconnect_wifi("
)

stub = OUT / "brookesia/service_helper/system/storage.hpp"
stub.parent.mkdir(parents=True, exist_ok=True)
stub.write_text(r'''
#pragma once
#include <cassert>
#include <cstdint>
#include <expected>
#include <functional>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
namespace esp_brookesia::service {
struct FunctionResult {
    bool success=true;
    std::string error_message,state="Idle";
    bool has_data()const{return true;}
    template<class T> const T& get_data()const{return state;}
};
struct ServiceBase {using FunctionResultHandler=std::function<void(FunctionResult&&)>;};
}
namespace esp_brookesia::service::helper {
struct Storage {
    struct Name { std::string name; };
    static inline bool enabled=false;
    static inline int fail=0, reads=0;
    static inline uint32_t timeout=0;
    static std::expected<Name,std::string> make_kv_namespace(
        std::initializer_list<std::string_view> parts, std::string_view separator, uint32_t ms) {
        assert(parts.size()==1 && *parts.begin()=="app.settings" && separator==".");
        assert(ms==timeout);
        if(fail==1) return std::unexpected("namespace unavailable");
        return Name{"resolved.namespace"};
    }
    static std::expected<Name,std::string> make_kv_key(
        std::initializer_list<std::string_view> parts, std::string_view separator, uint32_t ms) {
        assert(parts.size()==1 && *parts.begin()=="WifiEnabled" && separator==".");
        assert(ms==timeout);
        if(fail==2) return std::unexpected("key unavailable");
        return Name{"resolved.key"};
    }
    template<class T> static std::expected<T,std::string> get_key_value(
        const std::string &ns, const std::string &key, uint32_t ms) {
        assert(ns=="resolved.namespace" && key=="resolved.key" && ms==timeout);
        ++reads;
        if(fail==3) return std::unexpected("value absent");
        return enabled;
    }
};
struct Wifi { static inline bool available=false, running=true;
    enum class FunctionId {GetGeneralState,TriggerGeneralAction};
    enum class GeneralAction {Start,Stop};
    static inline std::string current_state="Idle";
    static inline std::vector<std::string> queued;
    static bool is_available(){return available;} static bool is_running(){return running;}
    template<class H> static bool call_function_async(FunctionId id,H handler){
        assert(id==FunctionId::GetGeneralState);
        service::FunctionResult result;result.state=current_state;handler(std::move(result));return true;
    }
    template<class H> static bool call_function_async(FunctionId id,const std::string& action,H handler){
        assert(id==FunctionId::TriggerGeneralAction);queued.push_back(action);
        service::FunctionResult result;handler(std::move(result));return true;
    }
};
struct SNTP { static inline bool available=false, running=true;
    static bool is_available(){return available;} static bool is_running(){return running;} };
}
''')

source = r'''
#include <cassert>
#include <expected>
#include <string>
#include <string_view>
#include <vector>
#include "brookesia/app_settings/wifi_preferences.hpp"
#define BROOKESIA_LOGW(...) ((void)0)
#define BROOKESIA_DESCRIBE_TO_STR(value) describe_action(value)
namespace esp_brookesia {
namespace system::core { struct AppContext {}; }
namespace app::settings {
using WifiHelper=service::helper::Wifi;
using SNTPHelper=service::helper::SNTP;
struct Binding { bool valid=false; bool is_valid() const{return valid;} };
std::string describe_action(WifiHelper::GeneralAction value){
    return value==WifiHelper::GeneralAction::Start ? "Start" : "Stop";
}
bool is_wifi_started_state(const std::string& state){
    return state=="Started" || state=="Connecting" || state=="Connected" || state=="Disconnecting";
}
class SettingsApp {
public:
    Binding wifi_service_binding_, sntp_service_binding_;
    struct {bool service_ready=false,enabled=false;} wifi_ui_state_;
    struct {bool service_ready=false;} time_zone_ui_state_;
    std::vector<int> wifi_event_connections_, sntp_event_connections_;
    int wifi_binds=0,sntp_binds=0,wifi_subscribes=0,sntp_subscribes=0;
    int wifi_refreshes=0,page_refreshes=0,summary_refreshes=0,time_refreshes=0;
    bool fail_wifi_subscription=false;
    std::vector<bool> applied;
    void bind_wifi_service(){++wifi_binds;wifi_service_binding_.valid=WifiHelper::available;}
    void bind_sntp_service(){++sntp_binds;sntp_service_binding_.valid=SNTPHelper::available;}
    void apply_wifi_enabled_preference_to_service(){applied.push_back(wifi_ui_state_.enabled);}
    std::expected<void,std::string> refresh_wifi_service_state(system::core::AppContext&){++wifi_refreshes;return {};}
    std::expected<void,std::string> refresh_wifi_page_state(system::core::AppContext&){++page_refreshes;return {};}
    std::expected<void,std::string> refresh_wifi_summary_state(system::core::AppContext&){++summary_refreshes;return {};}
    std::expected<void,std::string> refresh_time_zone_state(system::core::AppContext&){
        ++time_refreshes;time_zone_ui_state_.service_ready=true;return {};}
    void subscribe_wifi_events(){++wifi_subscribes;if(!fail_wifi_subscription)wifi_event_connections_.push_back(1);}
    void subscribe_sntp_events(){++sntp_subscribes;sntp_event_connections_.push_back(1);}
    void refresh_deferred_network_services(system::core::AppContext&);
};
RECOVERY
class PreferenceApp {
public:
    Binding wifi_service_binding_{true};
    struct {bool enabled=false;} wifi_ui_state_;
    uint64_t wifi_operation_generation_=0;
    void bind_wifi_service(){wifi_service_binding_.valid=WifiHelper::available;}
    void apply_wifi_enabled_preference_to_service(bool force_action=false);
};
APPLY_PREFERENCE
}
namespace system::super {
namespace core { constexpr int INVALID_TIMER_ID=0; }
constexpr std::string_view SUPER_STATUS_CLOCK_TIMER_NAME="clock";
struct Connection { bool live=false;bool connected()const{return live;} };
class ShellApp {
public:
    Connection wifi_event_connection_,sntp_event_connection_;
    int status_clock_timer_id_=7,wifi_calls=0,sntp_calls=0,clock_calls=0;
    void refresh_wifi_status(){++wifi_calls;wifi_event_connection_.live=true;}
    void subscribe_sntp_events(){++sntp_calls;sntp_event_connection_.live=true;}
    void refresh_status_clock(){++clock_calls;}
    void schedule_status_clock_timer(){status_clock_timer_id_=7;}
    std::expected<void,std::string> tick(std::string_view name="clock",int timer_id=7){
        CLOCK
        return {};
    }
};
}
}
int main(){
    using namespace esp_brookesia;
    using Storage=service::helper::Storage;
    using Wifi=service::helper::Wifi;
    using SNTP=service::helper::SNTP;
    assert(!app::settings::is_wifi_preference_restore_pending());
    Storage::timeout=2000;
    for(bool enabled:{false,true}){
        Storage::enabled=enabled;
        auto p=app::settings::read_wifi_enabled_preference(2000);
        assert(p && *p==enabled);
    }
    for(int stage:{1,2,3}){
        Storage::fail=stage;
        auto p=app::settings::read_wifi_enabled_preference(2000);
        assert(!p && !p.error().empty());
        assert(p.value_or(false)==false);
    }
    Storage::fail=0;
    system::core::AppContext context;
    app::settings::SettingsApp settings;
    // Settings was opened before services appeared, and the switch intent changed meanwhile.
    settings.wifi_ui_state_.enabled=true;
    settings.refresh_deferred_network_services(context);
    assert(settings.wifi_binds==0 && settings.applied.empty());
    settings.wifi_ui_state_.enabled=false;
    Wifi::available=true;
    app::settings::begin_wifi_preference_restore();
    assert(app::settings::is_wifi_preference_restore_pending());
    settings.refresh_deferred_network_services(context);
    assert(!settings.wifi_ui_state_.service_ready && settings.wifi_binds==0 && settings.applied.empty());
    app::settings::finish_wifi_preference_restore();
    settings.refresh_deferred_network_services(context);
    assert(settings.wifi_binds==1 && settings.applied==std::vector<bool>{false});
    assert(settings.wifi_subscribes==1 && settings.page_refreshes==1 && settings.summary_refreshes==1);
    assert(settings.wifi_refreshes==0);
    settings.refresh_deferred_network_services(context);
    assert(settings.applied.size()==1 && settings.wifi_subscribes==1 && settings.wifi_binds==1);
    SNTP::available=true;
    settings.refresh_deferred_network_services(context);
    settings.refresh_deferred_network_services(context);
    assert(settings.sntp_binds==1 && settings.sntp_subscribes==1 && settings.time_refreshes==1);
    // A failed event subscription retries without replaying the switch preference.
    app::settings::SettingsApp enabled_settings;
    enabled_settings.wifi_ui_state_.enabled=true;
    enabled_settings.fail_wifi_subscription=true;
    enabled_settings.refresh_deferred_network_services(context);
    assert(enabled_settings.applied==std::vector<bool>{true} && enabled_settings.wifi_refreshes==1);
    enabled_settings.fail_wifi_subscription=false;
    enabled_settings.refresh_deferred_network_services(context);
    enabled_settings.refresh_deferred_network_services(context);
    assert(enabled_settings.applied.size()==1 && enabled_settings.wifi_subscribes==2);
    // Reopening Settings with Wi-Fi already off must not send an invalid Stop.
    app::settings::PreferenceApp preference;
    for(const auto& state:{"Idle","Inited"}){
        Wifi::current_state=state;Wifi::queued.clear();
        preference.apply_wifi_enabled_preference_to_service();
        preference.apply_wifi_enabled_preference_to_service();
        assert(Wifi::queued.empty());
    }
    // A startup Start has been queued, but the reported state has not left Idle.
    // Pending restoration blocks UI commands; an explicit off must follow Start.
    Wifi::current_state="Idle";Wifi::queued={"Start"};
    app::settings::begin_wifi_preference_restore();
    preference.apply_wifi_enabled_preference_to_service(true);
    assert(Wifi::queued==std::vector<std::string>{"Start"});
    app::settings::finish_wifi_preference_restore();
    preference.wifi_ui_state_.enabled=false;
    preference.apply_wifi_enabled_preference_to_service(true);
    assert((Wifi::queued==std::vector<std::string>{"Start","Stop"}));
    bool radio_on=false;
    for(const auto& action:Wifi::queued)radio_on=action=="Start";
    assert(!radio_on);
    Wifi::queued.clear();Wifi::current_state="Started";
    preference.apply_wifi_enabled_preference_to_service();
    assert(Wifi::queued==std::vector<std::string>{"Stop"});
    Wifi::queued.clear();Wifi::current_state="Started";preference.wifi_ui_state_.enabled=true;
    preference.apply_wifi_enabled_preference_to_service();
    assert(Wifi::queued.empty());
    // A failed off save restores on while the queued Stop has not run yet.
    // Force the latest intent after Stop despite the still-Started state.
    Wifi::queued={"Stop"};
    preference.apply_wifi_enabled_preference_to_service(true);
    assert((Wifi::queued==std::vector<std::string>{"Stop","Start"}));
    radio_on=true;
    for(const auto& action:Wifi::queued)radio_on=action=="Start";
    assert(radio_on);
    system::super::ShellApp shell;
    Wifi::available=SNTP::available=false;
    shell.tick();
    assert(shell.wifi_calls==0 && shell.sntp_calls==0 && shell.clock_calls==1);
    Wifi::available=SNTP::available=true;
    shell.tick();shell.tick();
    assert(shell.wifi_calls==1 && shell.sntp_calls==1 && shell.clock_calls==3);
    shell.wifi_event_connection_.live=false;
    shell.tick();
    assert(shell.wifi_calls==2 && shell.sntp_calls==1);
    shell.tick("clock",999);
    assert(shell.clock_calls==4);
}
'''.replace("RECOVERY", recovery).replace("APPLY_PREFERENCE", apply_preference).replace(
    "\n        CLOCK\n", "\n" + clock + "\n"
)
cpp = OUT / "deferred_network_ui.cpp"
cpp.write_text(source)
include = ROOT / "components/brookesia_app_settings/include"

if os.name == "nt":
    vswhere = Path(os.environ["ProgramFiles(x86)"]) / "Microsoft Visual Studio/Installer/vswhere.exe"
    vs = subprocess.check_output([str(vswhere), "-latest", "-products", "*", "-requires",
        "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"], text=True).strip()
    env_script = OUT / "environment.cmd"
    env_script.write_text(f'@echo off\ncall "{vs}/VC/Auxiliary/Build/vcvars64.bat" >nul\nset\n', encoding="mbcs")
    env = dict(os.environ)
    for line in subprocess.check_output(["cmd.exe", "/d", "/c", str(env_script)], encoding="mbcs").splitlines():
        if "=" in line and not line.startswith("="):
            key, value = line.split("=", 1)
            env[key] = value
    compiler = Path(env["VCToolsInstallDir"]) / "bin/Hostx64/x64/cl.exe"
    exe = OUT / "deferred_network_ui.exe"
    command = [str(compiler), "/nologo", "/std:c++latest", "/EHsc", "/utf-8", f"/I{OUT}",
        f"/I{include}", str(cpp), f"/Fe{exe}", f"/Fo{OUT / 'deferred_network_ui.obj'}"]
else:
    env = None
    compiler = shutil.which("c++")
    assert compiler, "Host C++ compiler required"
    exe = OUT / "deferred_network_ui"
    command = [compiler, "-std=c++23", f"-I{OUT}", f"-I{include}", str(cpp), "-o", str(exe)]
compiled = subprocess.run(command, env=env, capture_output=True, text=True, errors="replace", timeout=60)
assert compiled.returncode == 0, compiled.stdout + compiled.stderr
subprocess.run([str(exe)], check=True, timeout=15)
print("Deferred network preferences and real Settings/Shell recovery behavior passed.")
