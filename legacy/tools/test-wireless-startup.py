"""Compile the real deferred wireless startup against small host API substitutes."""
from pathlib import Path
import os
import subprocess
import sys

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "examples/brookesia-minimal/main/wireless_startup.cpp"
OUT = ROOT / ".tools/wireless-startup-check"
INCLUDE = OUT / "include"
INCLUDE.mkdir(parents=True, exist_ok=True)

vswhere = Path(os.environ["ProgramFiles(x86)"]) / "Microsoft Visual Studio/Installer/vswhere.exe"
vs = subprocess.check_output([
    str(vswhere), "-latest", "-products", "*", "-requires",
    "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath",
], text=True).strip()
environment_script = OUT / "environment.cmd"
environment_script.write_text(
    f'@echo off\ncall "{vs}/VC/Auxiliary/Build/vcvars64.bat" >nul\nset\n', encoding="mbcs"
)
environment = dict(os.environ)
for line in subprocess.check_output(
    ["cmd.exe", "/d", "/c", str(environment_script)], encoding="mbcs"
).splitlines():
    if "=" in line and not line.startswith("="):
        key, value = line.split("=", 1)
        environment[key] = value
compiler = Path(environment["VCToolsInstallDir"]) / "bin/Hostx64/x64/cl.exe"

api = r'''
#pragma once
#include <algorithm>
#include <cstdint>
#include <expected>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace test {
inline void require(bool valid, const char *message) {
    if (!valid) throw std::runtime_error(message);
}
inline std::vector<std::string> events;
inline int scheduler = 1;
inline int create_failures = 0;
inline int create_attempts = 0;
inline int hosted_result = 0;
inline int hosted_calls = 0;
inline int wifi_adds = 0, wifi_binds = 0, wifi_starts = 0;
inline int sntp_adds = 0, sntp_binds = 0, preference_reads = 0, deletes = 0;
inline bool wifi_add_ok = true, wifi_bind_ok = true, wifi_start_ok = true;
inline bool sntp_add_ok = true, sntp_bind_ok = true;
inline int preference = 1; // -1: unavailable; 0: disabled; 1: enabled.
inline int active_results = 0;
inline void *preference_error_allocation = nullptr;
inline bool preference_error_freed = false;
inline bool preference_restore_pending = false;
inline void (*queued_task)(void *) = nullptr;
inline void *queued_argument = nullptr;
inline void before(const char *first, const char *second) {
    auto a = std::find(events.begin(), events.end(), first);
    auto b = std::find(events.begin(), events.end(), second);
    require(a != events.end() && b != events.end() && a < b, "Startup order changed");
}
inline void run_task() {
    require(queued_task != nullptr, "No task queued");
    auto callback = queued_task;
    queued_task = nullptr;
    callback(queued_argument);
}
struct Result {
    bool success;
    std::string failure = "mock start failure";
    explicit Result(bool value) : success(value) { ++active_results; }
    Result(const Result &) = delete;
    Result(Result &&other) : success(other.success) { ++active_results; }
    ~Result() { --active_results; }
    explicit operator bool() const { return success; }
    const std::string &error() const { return failure; }
};
}

constexpr int ESP_OK = 0;
inline const char *esp_err_to_name(int) { return "TEST_ERROR"; }
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
using BaseType_t = int;
constexpr int pdPASS = 1, pdFAIL = 0;
constexpr int taskSCHEDULER_NOT_STARTED = 0, taskSCHEDULER_RUNNING = 1;
constexpr int taskSCHEDULER_SUSPENDED = 2;
inline int xTaskGetSchedulerState() { return test::scheduler; }
inline BaseType_t xTaskCreate(void (*callback)(void *), const char *name,
        uint32_t stack, void *argument, unsigned priority, void *handle) {
    using namespace test;
    ++create_attempts;
    events.emplace_back("task_create");
    require(scheduler == taskSCHEDULER_RUNNING, "Task created before scheduler");
    require(std::string_view(name) == "ouse_wireless", "Wrong startup task");
    require(stack == 16 * 1024 && priority == 4 && handle == nullptr,
        "Startup must use one temporary internal 16 KiB xTaskCreate task");
    require(argument == nullptr, "Unexpected task argument");
    require(preference_restore_pending, "Settings can act before initial preference restore");
    if (create_failures > 0) { --create_failures; return pdFAIL; }
    require(queued_task == nullptr, "Duplicate startup task queued");
    queued_task = callback;
    queued_argument = argument;
    return pdPASS;
}
inline void vTaskDelete(void *task) {
    using namespace test;
    require(task == nullptr, "Startup deleted another task");
    require(active_results == 0, "C++ service-call locals survived task deletion");
    require(!preference_restore_pending, "Settings startup gate leaked after worker exit");
    require(preference != -1 || preference_error_freed,
        "Preference result local was not destroyed before task deletion");
    ++deletes;
    events.emplace_back("task_delete");
}
inline int esp_hosted_init() {
    test::require(test::preference_restore_pending, "Hosted started without gating Settings actions");
    test::require(test::scheduler == taskSCHEDULER_RUNNING,
        "Hosted called before scheduler");
    ++test::hosted_calls;
    test::events.emplace_back("hosted_init");
    return test::hosted_result;
}

namespace esp_brookesia::service {
struct ServiceBase {
    struct Attributes { std::string name; } attributes;
    explicit ServiceBase(std::string name) : attributes{std::move(name)} {}
    const Attributes &get_attributes() const { return attributes; }
    virtual ~ServiceBase() = default;
};
namespace wifi {
struct Wifi : ServiceBase {
    Wifi() : ServiceBase("WiFi") {}
    static Wifi &get_instance() { static Wifi value; return value; }
};
}
struct SNTP : ServiceBase {
    SNTP() : ServiceBase("SNTP") {}
    static SNTP &get_instance() { static SNTP value; return value; }
};
struct Binding {
    bool valid;
    bool is_valid() const { return valid; }
};
struct ServiceManager {
    static ServiceManager &get_instance() { static ServiceManager value; return value; }
    bool add_service(std::shared_ptr<ServiceBase> service) {
        test::require(test::hosted_calls == 1, "Service registered before Hosted");
        if (service->attributes.name == "WiFi") {
            ++test::wifi_adds; test::events.emplace_back("wifi_add");
            return test::wifi_add_ok;
        }
        test::require(service->attributes.name == "SNTP", "Unexpected service");
        ++test::sntp_adds; test::events.emplace_back("sntp_add");
        return test::sntp_add_ok;
    }
    Binding bind(std::string_view name) {
        if (name == "WiFi") {
            test::require(test::wifi_adds == 1, "Wi-Fi bound before add");
            ++test::wifi_binds; test::events.emplace_back("wifi_bind");
            return {test::wifi_bind_ok};
        }
        test::require(name == "SNTP" && test::sntp_adds == 1, "Unexpected bind");
        ++test::sntp_binds; test::events.emplace_back("sntp_bind");
        return {test::sntp_bind_ok};
    }
};
namespace helper {
struct Timeout {
    uint32_t milliseconds;
    explicit Timeout(uint32_t value) : milliseconds(value) {}
};
struct Wifi {
    enum class GeneralAction { Start };
    enum class FunctionId { TriggerGeneralAction };
    static std::string_view get_name() { return "WiFi"; }
    static test::Result call_function_sync(FunctionId function,
            const std::string &action, Timeout timeout) {
        test::require(function == FunctionId::TriggerGeneralAction && action == "Start",
            "Startup must delegate only Start, not duplicate Connect or SNTP Start");
        test::require(timeout.milliseconds == 2000 && test::wifi_binds == 1,
            "Start before bind or without bounded request");
        ++test::wifi_starts; test::events.emplace_back("wifi_start");
        return test::Result(test::wifi_start_ok);
    }
};
}
}
#define BROOKESIA_DESCRIBE_TO_STR(value) std::string_view("Start")

namespace esp_brookesia::app::settings {
inline void begin_wifi_preference_restore() { test::preference_restore_pending = true; }
inline void finish_wifi_preference_restore() { test::preference_restore_pending = false; }
inline bool is_wifi_preference_restore_pending() { return test::preference_restore_pending; }
inline std::expected<bool, std::string> read_wifi_enabled_preference(uint32_t timeout) {
    test::require(test::preference_restore_pending, "Preference restore is visible to user actions");
    test::require(timeout == 2000 && test::wifi_binds == 1,
        "Preference read must follow binding and have a timeout");
    ++test::preference_reads;
    test::events.emplace_back("preference");
    if (test::preference < 0) {
        std::string error(256, 'e'); // Heap-backed: observe its real destructor.
        test::preference_error_allocation = error.data();
        return std::unexpected(std::move(error));
    }
    return test::preference != 0;
}
}
'''
(INCLUDE / "test_api.hpp").write_text(api, encoding="utf-8")
headers = [
    "esp_err.h", "esp_hosted.h", "esp_log.h", "freertos/FreeRTOS.h", "freertos/task.h",
    "brookesia/app_settings/wifi_preferences.hpp", "brookesia/lib_utils/describe_helpers.hpp",
    "brookesia/service_helper/network/wifi.hpp", "brookesia/service_manager/service/manager.hpp",
    "brookesia/service_sntp.hpp", "brookesia/service_wifi.hpp",
]
for name in headers:
    header = INCLUDE / name
    header.parent.mkdir(parents=True, exist_ok=True)
    header.write_text('#pragma once\n#include "test_api.hpp"\n', encoding="utf-8")
(INCLUDE / "sdkconfig.h").write_text(
    "#pragma once\n#ifndef CONFIG_BROOKESIA_SERVICE_WIFI_ENABLE_AUTO_REGISTER\n"
    "#define CONFIG_BROOKESIA_SERVICE_WIFI_ENABLE_AUTO_REGISTER 0\n#endif\n"
    "#ifndef CONFIG_BROOKESIA_SERVICE_SNTP_ENABLE_AUTO_REGISTER\n"
    "#define CONFIG_BROOKESIA_SERVICE_SNTP_ENABLE_AUTO_REGISTER 0\n#endif\n",
    encoding="utf-8",
)

harness = r'''
#include "test_api.hpp"
#include "wireless_startup.hpp"
#include <cstdlib>
#include <iostream>
#include <new>
void *operator new(std::size_t size) {
    if (auto *value = std::malloc(size ? size : 1)) return value;
    throw std::bad_alloc();
}
void operator delete(void *value) noexcept {
    if (value && value == test::preference_error_allocation)
        test::preference_error_freed = true;
    std::free(value);
}
void operator delete(void *value, std::size_t) noexcept { ::operator delete(value); }

int main(int argc, char **argv) try {
    using namespace test;
    require(argc == 2, "Select one isolated scenario");
    const std::string scenario = argv[1];
    if (scenario == "scheduler") {
        scheduler = taskSCHEDULER_NOT_STARTED;
        require(!ouse::start_deferred_wireless(), "Accepted stopped scheduler");
        scheduler = taskSCHEDULER_SUSPENDED;
        require(!ouse::start_deferred_wireless(), "Accepted suspended scheduler");
        require(create_attempts == 0 && hosted_calls == 0, "Early side effects");
        scheduler = taskSCHEDULER_RUNNING;
    } else if (scenario == "task_retry") create_failures = 1;
    else if (scenario == "hosted_error") hosted_result = -1;
    else if (scenario == "wifi_add_error") wifi_add_ok = false;
    else if (scenario == "wifi_bind_error") wifi_bind_ok = false;
    else if (scenario == "pref_disabled") preference = 0;
    else if (scenario == "pref_missing") preference = -1;
    else if (scenario == "wifi_start_error") wifi_start_ok = false;
    else if (scenario == "sntp_add_error") sntp_add_ok = false;
    else if (scenario == "sntp_bind_error") sntp_bind_ok = false;
    else require(scenario == "success", "Unknown scenario");

    if (scenario == "task_retry") {
        require(!ouse::start_deferred_wireless(), "Task-create failure hidden");
        require(queued_task == nullptr && hosted_calls == 0, "Failed create initialized transport");
        require(!preference_restore_pending, "Failed task creation left Settings gated");
    }
    require(ouse::start_deferred_wireless(), "Could not queue startup");
    require(ouse::start_deferred_wireless(), "Duplicate request not idempotent");
    require(create_attempts == (scenario == "task_retry" ? 2 : 1), "Duplicate task creation");
    require(hosted_calls == 0 && wifi_adds == 0 && sntp_adds == 0,
        "Wireless initialized before queued task executed");
    run_task();
    require(hosted_calls == 1 && deletes == 1, "Startup task did not finish once");
    require(ouse::start_deferred_wireless(), "Completed request not idempotent");
    require(queued_task == nullptr && hosted_calls == 1,
        "Completed or partially initialized transport was retried");
    before("task_create", "hosted_init");
    before("hosted_init", "task_delete");
    if (scenario == "hosted_error") {
        require(wifi_adds == 0 && sntp_adds == 0 && preference_reads == 0,
            "Continued after Hosted returned failure");
    } else {
        require(wifi_adds == 1, "Wi-Fi not registered");
        before("hosted_init", "wifi_add");
        if (scenario == "wifi_add_error" || scenario == "wifi_bind_error") {
            require(wifi_binds == (wifi_add_ok ? 1 : 0) && preference_reads == 0,
                "Continued failed Wi-Fi registration/binding");
            require(sntp_adds == 0 && sntp_binds == 0 && wifi_starts == 0,
                "SNTP or Wi-Fi Start after unavailable Wi-Fi service");
        } else {
            require(wifi_binds == 1 && preference_reads == 1, "Preference path skipped");
            require(wifi_starts == (preference == 1 ? 1 : 0),
                "Stored Wi-Fi enabled preference not respected");
            require(sntp_adds == 1 && sntp_binds == (sntp_add_ok ? 1 : 0),
                "SNTP must be registered/bound once, without a second Start");
            before("wifi_bind", "preference");
            before("preference", "sntp_add");
            if (preference == -1)
                require(preference_error_freed, "Preference local leaked across task deletion");
        }
    }
    std::cout << "PASS " << scenario << '\n';
    return 0;
} catch (const std::exception &error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
}
'''
check = OUT / "check.cpp"
check.write_text(harness, encoding="utf-8")
executable = OUT / "check.exe"
command = [
    str(compiler), "/nologo", "/std:c++latest", "/EHsc", "/utf-8", "/MDd",
    f"/I{INCLUDE}", f"/I{SOURCE.parent}", str(SOURCE), str(check), f"/Fe{executable}",
]
compiled = subprocess.run(command, env=environment, cwd=OUT, capture_output=True,
                          text=True, errors="replace", timeout=90)
if compiled.returncode:
    print(compiled.stdout)
    print(compiled.stderr)
    raise SystemExit(compiled.returncode)
scenarios = [
    "scheduler", "success", "task_retry", "hosted_error", "wifi_add_error", "wifi_bind_error",
    "pref_disabled", "pref_missing", "wifi_start_error", "sntp_add_error", "sntp_bind_error",
]
for scenario in scenarios:
    subprocess.run([str(executable), scenario], check=True, timeout=15)

# The real implementation must reject either restored auto-registrar at compile time.
for service in ("WIFI", "SNTP"):
    rejected = subprocess.run([
        str(compiler), "/nologo", "/std:c++latest", "/EHsc", "/utf-8", "/c",
        f"/DCONFIG_BROOKESIA_SERVICE_{service}_ENABLE_AUTO_REGISTER=1",
        f"/I{INCLUDE}", str(SOURCE), f"/Fo{OUT / ('negative-' + service.lower() + '.obj')}",
    ], env=environment, cwd=OUT, capture_output=True, text=True, errors="replace", timeout=90)
    assert rejected.returncode and "Deferred wireless requires manual" in rejected.stdout, (
        service, rejected.stdout, rejected.stderr
    )

defaults = (ROOT / "examples/brookesia-minimal/sdkconfig.defaults").read_text(encoding="utf-8")
configured = (ROOT / "examples/brookesia-minimal/sdkconfig").read_text(encoding="utf-8")
for service in ("WIFI", "SNTP"):
    option = f"CONFIG_BROOKESIA_SERVICE_{service}_ENABLE_AUTO_REGISTER"
    assert f"{option}=n" in defaults
    assert f"{option}=y" not in configured
    assert f"# {option} is not set" in configured or f"{option}=n" in configured
print("PASS 11 isolated executions of real wireless_startup.cpp; auto-register guards; actual/manual configs")
