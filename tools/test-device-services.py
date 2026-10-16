"""Exercise actual audio, Wi-Fi and power UI, preserving device preferences.

This checks command/state behavior and real driver errors, not acoustic output
or a successful connection to an access point. No credentials are entered or
forgotten; the existing Wi-Fi enabled preference is restored afterwards.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import re
import sys
import time

from device import Device

spec = importlib.util.spec_from_file_location("ouse_device_system", Path(__file__).with_name("test-device-system.py"))
system = importlib.util.module_from_spec(spec)
spec.loader.exec_module(system)
UI, require = system.UI, system.require


def point(ui, node):
    bounds = ui.clip(node)
    require(bounds is not None, "Control is outside the visible viewport")
    return (bounds[0] + bounds[2]) // 2, (bounds[1] + bounds[3]) // 2


class Services:
    def __init__(self, ui):
        self.ui = ui

    def api(self):
        if self.ui.state()["active"] != "ouse.api-test":
            self.ui.open_app("ouse.api-test")

    def audio(self):
        self.api()
        node = self.ui.find_label(lambda text: " / Mute " in text and "Recorded:" in text, backward=True)
        text = node["text"]
        mode = re.search(r":\s*(idle|starting|playing|recording|playback|error)\s*/\s*(\d+)%\s*/\s*Mute\s*(true|false)", text)
        pcm = re.search(r"Mic:\s*(\d+)%\s*/\s*Recorded:\s*(\d+)\s*ms", text)
        require(mode is not None and pcm is not None, "Incomplete audio telemetry: " + text)
        state = {"state": mode.group(1), "volume": int(mode.group(2)), "muted": mode.group(3) == "true",
                 "peak": int(pcm.group(1)), "recorded_ms": int(pcm.group(2))}
        require(0 <= state["volume"] <= 100 and 0 <= state["peak"] <= 100 and
                0 <= state["recorded_ms"] <= 3000, "Invalid audio telemetry: " + repr(state))
        return state

    def power(self):
        self.api()
        node = self.ui.find_label(lambda text: "External:" in text and "Timeout:" in text, backward=True)
        text = node["text"]
        battery = re.search(r":\s*(--|\d+%)\s*/\s*(\d+)\s*mV", text)
        flags = re.search(r"External:\s*(true|false)\s*/\s*Charging:\s*(true|false)", text)
        timeout = re.search(r"Timeout:\s*(\d+)\s*s", text)
        require(battery is not None and flags is not None and timeout is not None, "Incomplete power telemetry: " + text)
        state = {"percent": -1 if battery.group(1) == "--" else int(battery.group(1)[:-1]),
                 "millivolts": int(battery.group(2)), "external": flags.group(1) == "true",
                 "charging": flags.group(2) == "true", "timeout": int(timeout.group(1))}
        require(-1 <= state["percent"] <= 100 and 0 <= state["millivolts"] <= 6000 and
                state["timeout"] in (0, 15, 30, 60, 120, 300), "Invalid power telemetry: " + repr(state))
        return state

    def wifi(self):
        self.api()
        node = self.ui.find_label(lambda text: "APs:" in text and " / Error:" in text)
        text = node["text"]
        enabled = re.search(r":[ \t]*(ON|OFF)[ \t]*/[ \t]*([^\n]*)", text)
        result = re.search(r"APs:\s*(\d+)\s*/\s*Error:\s*(-?\d+)", text)
        require(enabled is not None and result is not None, "Incomplete Wi-Fi telemetry: " + text)
        state = {"enabled": enabled.group(1) == "ON", "ip": enabled.group(2).strip(),
                 "aps": int(result.group(1)), "error": int(result.group(2))}
        require(0 <= state["aps"] <= 12, "Invalid Wi-Fi AP count: " + repr(state))
        return state

    def wait_audio_idle(self, timeout=12):
        deadline, seen = time.monotonic() + timeout, []
        while True:
            audio = self.audio()
            seen.append(audio)
            require(audio["state"] != "error", "Actual audio driver reported an error: " + repr(audio))
            if audio["state"] == "idle":
                return audio, seen
            require(time.monotonic() < deadline, "Audio did not finish before timeout: " + repr(seen))
            time.sleep(.15)

    def volume(self, target):
        self.api()
        if self.audio()["volume"] == target:
            return
        self.ui.find_label(("音量", "Volume"))
        for _ in range(6):
            nodes = self.ui.tree()
            # The page orders each slider immediately after its title. Match
            # that relationship before applying a 0..100 range; the other
            # slider belongs to brightness and has a different range.
            candidates = []
            for index, node in enumerate(nodes):
                if node["type"] != "slider" or not self.ui.clip(node):
                    continue
                prior = next((n for n in reversed(nodes[:index]) if n["type"] == "label"), None)
                if prior and prior["text"] in ("音量", "Volume"):
                    candidates.append(node)
            if candidates:
                require(len(candidates) == 1, "Volume slider was not uniquely identifiable")
                self.ui.drag_slider(candidates[0], target, 0, 100)
                actual = self.audio()
                require(actual["volume"] == target, f"Volume target {target}, actual {actual['volume']}")
                return
            self.ui.scroll(True, nodes)
        raise RuntimeError("Could not locate the volume slider")

    def muted(self, target):
        if self.audio()["muted"] != target:
            self.ui.tap_label(("切换静音", "Toggle mute"))
            require(self.audio()["muted"] == target, "Mute preference did not apply")

    def enabled(self, target):
        current = self.wifi()
        if current["enabled"] != target:
            self.ui.tap_label(("切换 Wi-Fi", "Toggle Wi-Fi"))
            deadline = time.monotonic() + 20
            while True:
                current = self.wifi()
                if current["enabled"] == target:
                    break
                require(time.monotonic() < deadline, "Wi-Fi switch did not apply")
                time.sleep(.2)
            time.sleep(.6)  # let the background worker process stop/start
        return current

    def native_page(self, texts):
        self.ui.open_app("ouse.settings")
        nodes = self.ui.tree()
        back = [node for node in nodes if node["type"] == "label" and node["text"] in ("返回设置", "Settings", "\uf053") and
                any(parent["type"] == "button" for parent in node["_parents"]) and self.ui.clip(node)]
        if back:
            self.ui.device.tap(*point(self.ui, back[0]))
        self.ui.tap_label(texts)

    def timeout(self, seconds):
        if self.power()["timeout"] == seconds:
            return
        labels = {0: ("从不", "Never"), 15: ("15 秒", "15 seconds"), 30: ("30 秒", "30 seconds"),
                  60: ("1 分钟", "1 minute"), 120: ("2 分钟", "2 minutes"), 300: ("5 分钟", "5 minutes")}
        require(seconds in labels, "Unsupported screen timeout")
        self.native_page(("电源", "Power"))
        self.ui.tap_label(("自动熄屏", "Screen timeout"))
        self.ui.tap_label(labels[seconds])
        require(self.power()["timeout"] == seconds, "Screen timeout did not apply")

    def scan_finished(self, request_scan=True):
        self.native_page("Wi-Fi")
        if request_scan:
            self.ui.tap_label(("搜索网络", "Scan networks"))
        started, deadline, saw_scanning = time.monotonic(), time.monotonic() + 22, False
        statuses = []
        while True:
            nodes = self.ui.tree()
            lines = [n["text"] for n in nodes if n["type"] == "label"]
            searching = any(text.split("\n", 1)[0] in ("正在搜索", "Scanning") for text in lines)
            saw_scanning |= searching
            failures = [text for text in lines if text.split("\n", 1)[0] in
                        ("连接失败，请重试", "Connection failed. Try again", "无线服务不可用", "Wi-Fi unavailable")]
            require(not failures, "Wi-Fi reported an operation error: " + repr(failures))
            settled = any(text.split("\n", 1)[0] in ("已连接", "Connected", "未连接", "Disconnected") for text in lines)
            statuses.append({"time": time.time(), "scanning": searching, "settled": settled})
            if settled and not searching and time.monotonic() - started >= 1:
                break
            require(time.monotonic() < deadline, "Wi-Fi scan did not reach a settled state")
            time.sleep(.25)
        state = self.wifi()
        require(state["enabled"] and state["error"] == 0, "Wi-Fi scan returned an error: " + repr(state))
        self.ui.record("wifi_scan", observed_scanning=saw_scanning, state=state, observations=statuses)
        return state

    def dismiss_error_dialog(self):
        nodes = self.ui.tree()
        confirms = [n for n in nodes if n["type"] == "label" and n["text"] in ("确定", "OK") and
                    any(parent["type"] == "button" for parent in n["_parents"]) and self.ui.clip(n)]
        if confirms:
            self.ui.device.tap(*point(self.ui, confirms[-1]))

    def restore(self, original):
        errors = []
        if not self.ui.alive():
            self.ui.record("service_restore_skipped", reason="panic, reboot or serial failure")
            return

        def restore_one(name, operation):
            if not self.ui.alive():
                errors.append({"preference": name, "error": "device stopped responding; no input sent"})
                return
            try:
                self.dismiss_error_dialog()
                operation()
                self.ui.record("service_restored", preference=name)
            except Exception as error:
                errors.append({"preference": name, "error": str(error)})
        if "audio" in original:
            def restore_audio():
                self.api()
                if self.audio()["state"] != "idle":
                    self.ui.tap_label(("停止", "Stop"))
                    self.wait_audio_idle()
                self.volume(original["audio"]["volume"])
                self.muted(original["audio"]["muted"])
            restore_one("audio", restore_audio)
        if "wifi" in original:
            restore_one("wifi_enabled", lambda: self.enabled(original["wifi"]["enabled"]))
        if "power" in original:
            restore_one("screen_timeout", lambda: self.timeout(original["power"]["timeout"]))
        if "brightness" in original:
            def restore_brightness():
                if self.ui.read_brightness() != original["brightness"]:
                    self.ui.api_brightness(original["brightness"])
            restore_one("brightness", restore_brightness)
        restore_one("home", self.ui.home)
        require(not errors, "Service preference restore errors: " + repr(errors))


def audio_test(services, rounds):
    ui = services.ui
    services.volume(20)
    services.muted(False)
    services.muted(True)
    services.muted(False)
    for index in range(rounds):
        ui.tap_label(("播放测试音", "Test tone"))
        tone, tone_seen = services.wait_audio_idle()
        ui.tap_label(("录音 3 秒", "Record 3 s"))
        recording, record_seen = services.wait_audio_idle()
        require(recording["recorded_ms"] == 3000, "Recording did not produce three seconds of PCM: " + repr(recording))
        # Position adjacent replay/stop controls before starting. No scrolling or
        # page switch is needed while the three-second recording is playing.
        ui.find_label(("停止", "Stop"))
        nodes = ui.tree()
        replay = next((n for n in nodes if n["type"] == "label" and n["text"] in ("回放录音", "Play recording") and ui.clip(n)), None)
        stop = next((n for n in nodes if n["type"] == "label" and n["text"] in ("停止", "Stop") and ui.clip(n)), None)
        require(replay is not None and stop is not None, "Replay and stop controls are not simultaneously visible")
        ui.device.tap(*point(ui, replay))
        time.sleep(.15)
        stop_start = time.monotonic()
        ui.device.tap(*point(ui, stop))
        stopped, stop_seen = services.wait_audio_idle()
        ui.record("audio_round", round=index + 1, tone=tone, recording=recording, stopped=stopped,
                  stop_wait_seconds=time.monotonic() - stop_start, observations=tone_seen + record_seen + stop_seen,
                  claim="driver completion/PCM duration/stop; acoustic output is not measured")
        print(f"PASS audio commands: {index + 1}/{rounds}", flush=True)


def wifi_test(services, rounds):
    ui = services.ui
    services.enabled(True)
    for index in range(rounds):
        scan = services.scan_finished()
        services.enabled(False)
        off = services.wifi()
        require(not off["enabled"] and off["error"] == 0, "Wi-Fi did not turn off cleanly")
        services.enabled(True)
        on = services.scan_finished(request_scan=False)
        ui.record("wifi_round", round=index + 1, scan=scan, off=off, on=on,
                  claim="scan and enabled-state recovery; no AP connection is asserted")
        print(f"PASS Wi-Fi scan/off/on: {index + 1}/{rounds}", flush=True)


def power_test(services, rounds):
    ui = services.ui
    power = services.power()
    ui.record("power_telemetry", state=power, claim="cached PMIC telemetry; absent battery remains unknown")
    for index in range(rounds):
        # Observe explicit Screen off using a control whose effect is measurable.
        before_timeout = services.power()["timeout"]
        ui.find_label(("立即熄屏", "Screen off"))
        nodes = ui.tree()
        timeout = next((n for n in nodes if n["type"] == "label" and n["text"] in
                        ("切换熄屏时间", "Change screen timeout") and ui.clip(n)), None)
        sleep = next((n for n in nodes if n["type"] == "label" and n["text"] in
                      ("立即熄屏", "Screen off") and ui.clip(n)), None)
        require(timeout is not None and sleep is not None, "Screen off and timeout controls are not simultaneously visible")
        timeout_point = point(ui, timeout)
        ui.device.tap(*point(ui, sleep))
        ui.device.tap(*timeout_point)  # wakes; the timeout button must not execute
        require(services.power()["timeout"] == before_timeout, "The waking touch activated its underlying control")
        ui.tap_label(("切换熄屏时间", "Change screen timeout"))
        require(services.power()["timeout"] == (0 if before_timeout else 60), "Input did not resume after waking")

        # Test inactivity sleep against Counter's unambiguous increment action.
        services.timeout(15)
        ui.open_app("ouse.counter")
        plus = ui.find_label("+1")
        values = [int(n["text"]) for n in ui.tree() if n["type"] == "label" and n["text"].isdecimal()]
        require(len(values) == 1, "Counter value must be visible beside its increment control")
        before = values[0]
        plus_point = point(ui, plus)
        ui.record("sleep_wait", round=index + 1, seconds=17, counter=before)
        print(f"Waiting for 15-second inactivity sleep: {index + 1}/{rounds}", flush=True)
        time.sleep(17)
        ui.device.tap(*plus_point)
        first = system.counter_value(ui)
        require(first == before, "The first touch after inactivity changed Counter")
        ui.device.tap(*plus_point)
        second = system.counter_value(ui)
        require(second == before + 1, "The second touch after waking did not increment Counter")
        ui.record("power_round", round=index + 1, before=before, after_waking_touch=first, after_second_touch=second,
                  claim="observable wake-contact consumption; no optical backlight measurement")
        ui.home()
        print(f"PASS explicit/inactivity sleep and wake: {index + 1}/{rounds}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--group", choices=("all", "audio", "wifi", "power"), default="all")
    parser.add_argument("--rounds", type=int, default=3)
    parser.add_argument("--port", default="COM10")
    parser.add_argument("--elf")
    parser.add_argument("--output")
    args = parser.parse_args()
    if not 1 <= args.rounds <= 50:
        parser.error("--rounds must be 1..50")
    report = {"group": args.group, "rounds_requested": args.rounds, "status": "running",
              "started": time.time(), "events": [], "groups": []}
    device = ui = services = None
    original, code, testing_started = {}, 0, False

    def save():
        if device is not None:
            (device.output / "services-result.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    try:
        device = Device(args.port, args.output, args.elf)
        report["output"] = str(device.output.resolve())
        require("debug=1" in device.request("HELLO", timeout=8), "Device debug controls are unavailable")
        ui, services = UI(device, report, save), None
        services = Services(ui)
        original["brightness"] = ui.read_brightness()
        original["audio"] = services.audio()
        original["power"] = services.power()
        original["wifi"] = services.wifi()
        report["original_preferences"] = original
        save()
        require(original["audio"]["state"] in ("idle", "error"), "Audio is already in use; preserve the active operation")
        testing_started = True
        groups = ("audio", "wifi", "power") if args.group == "all" else (args.group,)
        operations = {"audio": audio_test, "wifi": wifi_test, "power": power_test}
        for group in groups:
            ui.checkpoint(group + "_before")
            operations[group](services, args.rounds)
            ui.checkpoint(group + "_after")
            report["groups"].append({"group": group, "rounds": args.rounds, "passed": True})
            save()
        report["status"] = "passed"
    except (Exception, KeyboardInterrupt) as error:
        report["status"] = "interrupted" if isinstance(error, KeyboardInterrupt) else "failed"
        report["error"] = {"type": type(error).__name__, "message": str(error)}
        code = 130 if isinstance(error, KeyboardInterrupt) else 1
        print("Service UI test failed:", error, file=sys.stderr, flush=True)
    finally:
        if device is not None:
            if services is not None and original and testing_started and ui.alive():
                try:
                    services.restore(original)
                    report["preferences_restored"] = True
                except Exception as error:
                    report["restore_error"] = str(error)
                    report["status"] = "failed"
                    code = 1
            elif original and not testing_started:
                report["preferences_restore_skipped"] = "preflight stopped before test preference changes"
            elif original:
                report["preferences_restore_skipped"] = "panic, reboot or serial failure; no further input sent"
            if ui is not None and ui.last_tree is not None:
                (device.output / "last-tree.json").write_text(json.dumps(ui.last_tree, ensure_ascii=False, indent=2), encoding="utf-8")
            save()
            try:
                device.close()
            except Exception as error:
                report["capture_close_error"] = str(error)
                report["status"] = "failed"
                code = 1
            report["panic_seen"] = bool(device.capture.panic)
            report["unexpected_reboot"] = device.rebooted
            if report["panic_seen"] or report["unexpected_reboot"] or device.error:
                report["status"] = "failed"
                code = 1
            report["finished"] = time.time()
            save()
            print("Evidence:", device.output.resolve(), flush=True)
    return code


if __name__ == "__main__":
    raise SystemExit(main())
