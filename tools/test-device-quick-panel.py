"""Short real-board check of the native compact quick panel.

Uses the production pointer path, actual LCD JPEGs and one temporary BPK. It
never builds, flashes, resets, erases, plays audio or changes Wi-Fi credentials.
The fixture ID must be absent; its settings and all initial preferences are
restored before removal. Device owns the sole UART handle and raw evidence.
"""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import sys
import time

import bpk
from device import Device

ROOT = Path(__file__).resolve().parents[1]
APP_ID = "user.quick-test"
PREFERENCE_KEYS = ("theme", "locale", "font", "dpi")
GEAR = "\uf013"
CASES = [(100, "light", "zh_CN"), (100, "dark", "en"),
         (115, "light", "en"), (115, "dark", "zh_CN"),
         (130, "light", "zh_CN"), (130, "dark", "en")]


def require(value, message):
    if not value:
        raise RuntimeError(message)


def healthy(device):
    return not device.capture.panic and not device.rebooted and device.error is None


def save_json(path, value):
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")
    temporary.replace(path)


def wait_for(probe, predicate, message, timeout=9):
    deadline = time.monotonic() + timeout
    while True:
        value = probe()
        if predicate(value):
            return value
        require(time.monotonic() < deadline, message + ": " + repr(value))
        time.sleep(.08)


def installed_ids(device):
    value = device.request("LIST", timeout=8)
    return [] if value == "-" else sorted(value.split(","))


def runtime(device):
    return {key: int(value) for key, value in
            (item.split("=", 1) for item in device.request("DBG_RUNTIME").split(","))}


def upload(device, data):
    device.request("BEGIN", f"{len(data):08x}:{bpk.crc(data):08x}", timeout=8)
    try:
        for offset in range(0, len(data), 512):
            chunk = data[offset:offset + 512]
            reply = device.request("DATA", f"{offset:08x}:{chunk.hex()}", timeout=8)
            require(int(reply) == offset + len(chunk), "Wrong package upload offset")
        require(device.request("END", timeout=25, attempts=1) == APP_ID, "Wrong installed fixture ID")
    except BaseException:
        if healthy(device):
            try:
                device.request("ABORT", attempts=1)
            except Exception:
                pass
        raise


SCRIPT = r'''"use strict";
function snapshot(){let d=Ouse.display.get(),a=Ouse.audio.get(),t=Ouse.theme.get();return {
 t:t.mode,l:Ouse.locale.get(),f:d.fontPercent,b:d.brightness,v:a.volume,m:a.muted,a:t.colors.accent,s:t.colors.surface};}
const original=__ORIGINAL__ || snapshot();
let currentCase=-1;
const cases=__CASES__;
function apply(p){Ouse.theme.set(p.t);Ouse.locale.set(p.l);Ouse.display.setFontPercent(p.f);
 Ouse.display.setBrightness(p.b,true);Ouse.audio.setVolume(p.v);Ouse.audio.setMuted(p.m);}
function render(){Ouse.ui.setText("initial","initial="+JSON.stringify(original));
 Ouse.ui.setText("current","current="+JSON.stringify(snapshot()));Ouse.ui.setText("case","case="+currentCase);}
Ouse.ui.on("next","click",()=>{currentCase=(currentCase+1)%cases.length;
 const c=cases[currentCase];Ouse.theme.set(c[1]);Ouse.locale.set(c[2]);Ouse.display.setFontPercent(c[0]);render();});
Ouse.ui.on("read","click",render);
Ouse.ui.on("restore","click",()=>{apply(original);render();});
globalThis.app={onResume:render};
Ouse.timers.setInterval(render,200);
if(__RESTORE__)apply(original);
render();
'''


def fixture(device, original=None, restore=False):
    source = device.output / ("recovery-source" if restore else "fixture-source")
    source.mkdir(exist_ok=True)
    manifest = dict(runtime="ouse-core", api=1, id=APP_ID, name="Quick Panel Test", icon="icon.bin")
    children = [dict(type="label", id="title", text="Quick panel test", width="fill", height="content", fontSize=16)]
    for name in ("initial", "current", "case"):
        children.append(dict(type="label", id=name, text=name + "=loading", width="fill", height="content", fontSize=10))
    for name, title in (("next", "Next case"), ("read", "Read state"), ("restore", "Restore original")):
        children.append(dict(type="button", id=name, text=title, width="fill", height=44, fontSize=14))
    page = dict(api=1, units="dp", root=dict(type="container", id="root", width="fill", height="fill",
                layout="column", padding=10, gap=6, scroll=True, children=children))
    script = SCRIPT.replace("__ORIGINAL__", json.dumps(original)).replace("__CASES__", json.dumps(CASES)).replace("__RESTORE__", "true" if restore else "false")
    for name, value in (("manifest.json", manifest), ("page.json", page)):
        (source / name).write_text(json.dumps(value), encoding="utf-8")
    (source / "app.js").write_text(script, encoding="utf-8")
    (source / "icon.bin").write_bytes(b"OIC1" + struct.pack("<HH", 4, 4) + bytes((50, 160, 220, 255)) * 16)
    path = device.output / ("recovery.bpk" if restore else "quick-test.bpk")
    require(bpk.pack(source, path)["id"] == APP_ID, "Wrong fixture package ID")
    return path.read_bytes()


class UI:
    def __init__(self, device):
        self.device = device
        state = device.state()
        self.width, self.height, self.dpi = state["width"], state["height"], state["dpi"]

    def dp(self, value):
        return round(value * self.dpi / 160)

    def tree(self, filename=None):
        tree = self.device.tree()
        if filename:
            save_json(self.device.output / filename, tree)
        stack = []
        for index, node in enumerate(tree["nodes"]):
            while stack and stack[-1]["depth"] >= node["depth"]:
                stack.pop()
            node["_parents"], node["_index"] = tuple(stack), index
            stack.append(node)
        return tree

    def clipped(self, node):
        box = [0, 0, self.width - 1, self.height - 1]
        for parent in (*node["_parents"], node):
            a, b, c, d = parent["bounds"]
            box = [max(box[0], a), max(box[1], b), min(box[2], c), min(box[3], d)]
        return box

    def fully_visible(self, node):
        return self.clipped(node) == node["bounds"]

    def text(self, prefix):
        values = [n["text"] for n in self.tree()["nodes"] if n.get("text", "").startswith(prefix)]
        require(len(values) == 1, "Missing or ambiguous fixture state: " + repr((prefix, values)))
        return values[0][len(prefix):]

    def current(self):
        return json.loads(self.text("current="))

    def find(self, text, tree=None, right=False):
        nodes = [n for n in (tree or self.tree())["nodes"] if n.get("text") == text and self.fully_visible(n)]
        if right:
            nodes = [n for n in nodes if n["bounds"][0] > self.width * .65]
        require(nodes, "Visible UI text not found: " + repr(text))
        return max(nodes, key=lambda node: node["depth"])

    def tap_node(self, node):
        a, b, c, d = self.clipped(node)
        self.device.tap((a + c) // 2, (b + d) // 2)

    def tap(self, text, right=False):
        self.tap_node(self.find(text, right=right))

    def open_fixture(self):
        self.device.request("DBG_OPEN", APP_ID, timeout=10)
        wait_for(self.device.state, lambda s: s["active"] == APP_ID and not s["quick"] and not s.get("notifications"), "Fixture did not open")
        wait_for(lambda: [n.get("text", "") for n in self.tree()["nodes"]],
                 lambda values: any(v.startswith("current={") for v in values), "Fixture JS did not become ready")

    def close_panel(self):
        self.device.swipe(self.width // 2, self.height-self.dp(10), self.width // 2, self.dp(100), 220)
        return wait_for(self.device.state, lambda s: not s["quick"] and not s.get("notifications") and not s["tracking"], "Panel did not close")

    def open_panel(self, notifications=False, tracking=False):
        x, y = (self.width // 4 if notifications else self.width * 3 // 4), self.dp(4)
        middle, end = self.dp(100), self.dp(160)
        try:
            self.device.touch(x, y, True)
            for target in (self.dp(24), self.dp(48), self.dp(76), middle):
                self.device.touch(x, target, True)
                time.sleep(.025)
            state = wait_for(self.device.state, lambda s: s["tracking"] and bool(s.get("notifications")) == notifications and bool(s["quick"]) != notifications,
                             "Initial side did not select the requested tracking panel", timeout=5)
            require(abs(state["quick_y"] - (-self.height + middle - y)) <= self.dp(4), "Panel did not follow the actual pointer")
            if tracking:
                self.device.screenshot(self.device.output / "quick-following.jpg")
            self.device.touch(x, end, True)
        finally:
            if healthy(self.device):
                self.device.touch(x, end, False)
        settled = wait_for(self.device.state, lambda s: not s["tracking"] and s["quick_y"] == 0 and bool(s.get("notifications")) == notifications and bool(s["quick"]) != notifications,
                           "Panel release did not settle fully open")
        return {"following": state, "settled": settled}

    def panel_layout(self, filename):
        tree = self.tree(filename)
        wifi = self.find("Wi-Fi", tree)
        chinese = self.device.state()["locale"] == 0
        power = self.find("电源" if chinese else "Power", tree)
        sound = self.find("声音" if chinese else "Sound", tree)
        theme = self.find("主题" if chinese else "Theme", tree)
        # Native structure: title -> tile -> scrolling body -> quick root.
        panel = wifi["_parents"][-3]
        nodes = tree["nodes"][panel["_index"] + 1:]
        nodes = nodes[:next((i for i, n in enumerate(nodes) if n["depth"] <= panel["depth"]), len(nodes))]
        body = wifi["_parents"][-2]
        direct_rows = [n for n in nodes if n["depth"] == body["depth"] + 1 and body in n["_parents"]]
        require(len(direct_rows) == 5, "Quick body must contain only four tiles and one brightness capsule")
        forbidden = ("字号：标准", "字号：较大", "字号：最大", "Font: Standard", "Font: Larger")
        require(not any(n.get("text") in forbidden for n in nodes), "Font rows are still in the panel")
        require(not any(n.get("text", "").endswith("%") and n.get("text", "")[:-1].isdigit() for n in nodes), "A numeric percentage is still displayed in the quick panel")
        sliders = sorted([n for n in nodes if n["type"] == "slider"], key=lambda n: n["bounds"][1])
        require(len(sliders) == 1, "Expected only one brightness slider")
        actions = [self.find(GEAR, tree, right=True), theme, sound]
        for node in (wifi, power, sound, theme, *sliders, *actions):
            require(self.fully_visible(node), "A core quick control is clipped: " + repr(node["bounds"]))
            require(node["bounds"][3] < self.height - self.dp(28), "A core control needs first-screen scrolling")
        for node in (wifi, power, sound, theme, *actions):
            target = next((p for p in reversed(node["_parents"]) if p["clickable"]), None)
            require(target is not None, "No actual click target for quick action")
            a, b, c, d = target["bounds"]
            require(c - a + 1 >= self.dp(40) and d - b + 1 >= self.dp(40), "Touch target is smaller than 40 dp")
        for node in sliders:
            require(node["bounds"][3] - node["bounds"][1] + 1 >= self.dp(32), "Slider track shrank below 32 dp")
        current = next(n["text"][8:] for n in tree["nodes"] if n.get("text", "").startswith("current={"))
        current = json.loads(current)
        for action, selected in ((theme, current["t"] == "dark"), (sound, not current["m"] and current["v"] > 0)):
            target = next(p for p in reversed(action["_parents"]) if p["clickable"])
            require(target["bg"].lower() == current["a" if selected else "s"].lower(), "Quick action color disagrees with actual native state")
        return tree, sliders, {"wifi": wifi["bounds"], "power": power["bounds"], "sound": sound["bounds"], "theme": theme["bounds"], "sliders": [n["bounds"] for n in sliders],
                               "actions": [n["bounds"] for n in actions], "panel_nodes": len(nodes)}

    def slider_drag(self, node, fraction):
        a, b, c, d = node["bounds"]
        y, start, end = (b + d) // 2, (a + c) // 2, round(a + (c - a) * fraction)
        self.device.swipe(start, y, end, y, 180)


def preferences(state):
    return {key: state[key] for key in PREFERENCE_KEYS}


def restore(device, ui, original, allow_recovery=True):
    try:
        ui.open_fixture()
        ui.tap("Restore original")
        return wait_for(ui.current, lambda state: state == original, "Original hardware/preferences did not restore")
    except Exception:
        if not allow_recovery or not healthy(device):
            raise
        # Own ID only. Replacing a faulted fixture drops its old JS context;
        # the recovery copy restores the PC-recorded settings at startup.
        upload(device, fixture(device, original, restore=True))
        ui.open_fixture()
        return wait_for(ui.current, lambda state: state == original, "Recovery fixture could not restore original settings")


def run(device, args, report, save):
    hello = device.request("HELLO", timeout=8, attempts=3)
    require("storage=ready" in hello and "debug=1" in hello, "Storage/debug unavailable")
    expected = hashlib.sha256(Path(args.elf).read_bytes()).hexdigest()
    require(device.request("DBG_BUILD") == expected, "Board firmware does not match the supplied ELF")
    original_ids, original_state = installed_ids(device), device.state()
    require(APP_ID not in original_ids, "Fixture ID exists; refusing overwrite")
    require(not any(original_state.get(key) for key in ("quick", "notifications", "recents", "tracking")), "An original panel/gesture is open; preserve it and stop")
    report.update(hello=hello, elf_sha256=expected, original_ids=original_ids, original_state=original_state,
                  original_runtime=runtime(device), owns_fixture=True)
    save()
    data = fixture(device)
    report["fixture_sha256"] = hashlib.sha256(data).hexdigest()
    upload(device, data)
    ui = UI(device)
    ui.open_fixture()
    original = json.loads(ui.text("initial="))
    report["original_values"] = original
    require((original["t"] == ("dark" if original_state["theme"] else "light") and
             original["l"] == ("en" if original_state["locale"] else "zh_CN") and original["f"] == original_state["font"]),
            "Fixture initial preferences differ from the recorded system state")
    save()
    report["tracking"] = ui.open_panel(tracking=True)
    ui.close_panel()
    report["notification_side"] = ui.open_panel(notifications=True)
    device.screenshot(device.output / "notifications-left.jpg")
    ui.close_panel()
    for index, (font, theme, locale) in enumerate(CASES):
        ui.open_fixture()
        ui.tap("Next case")
        wait_for(lambda: ui.text("case="), lambda value: value == str(index), "Fixture case button did not execute")
        wait_for(device.state, lambda s: s["font"] == font and s["theme"] == (theme == "dark") and s["locale"] == (locale == "en"), "Native preferences did not finish applying")
        ui.open_panel()
        name = f"quick-{font}-{theme}-{locale}"
        _, _, bounds = ui.panel_layout(name + ".json")
        device.screenshot(device.output / (name + ".jpg"))
        report["stages"].append(dict(stage=name, bounds=bounds))
        save()
        ui.close_panel()

    ui.open_panel()
    _, sliders, bounds = ui.panel_layout("quick-actions-before.json")
    brightness_fraction = .30 if original["b"] > 55 else .72
    expected_brightness = 10 + 90 * brightness_fraction
    ui.slider_drag(sliders[0], brightness_fraction)
    ui.close_panel()
    changed = wait_for(ui.current, lambda v: abs(v["b"] - expected_brightness) <= 8, "Native brightness did not follow actual drag")
    require(changed["b"] != original["b"], "Brightness slider did not change its native value")
    require(changed["v"] == original["v"] and changed["m"] == original["m"], "Brightness interaction changed audio settings")
    report["slider_values"] = changed
    ui.open_panel()
    ui.tap("主题" if device.state()["locale"] == 0 else "Theme")
    wait_for(device.state, lambda s: s["theme"] == 0, "Theme tile did not switch dark to light")
    wait_for(ui.current, lambda v: v["t"] == "light", "Theme did not reach actual native state")
    ui.panel_layout("quick-actions-after.json")
    device.screenshot(device.output / "quick-actions-after.jpg")
    ui.close_panel()
    report["theme_values"] = wait_for(ui.current, lambda v: v["t"] == "light", "Theme did not remain applied")
    chinese = device.state()["locale"] == 0
    for title in ("Wi-Fi", "电源" if chinese else "Power", "声音" if chinese else "Sound"):
        expected_heading = title
        ui.open_fixture()
        ui.open_panel()
        ui.tap(title)
        wait_for(device.state, lambda s: s["active"] == "ouse.settings" and not s["quick"], "Quick tile did not open native settings")
        # A matching page heading must be close to the top; a matching row on
        # the general settings page does not prove the intended page opened.
        wait_for(lambda: ui.tree()["nodes"], lambda nodes: any(n.get("text") == expected_heading and n["bounds"][1] < ui.dp(110) for n in nodes), "Quick tile opened the wrong settings page")
        device.screenshot(device.output / ("tile-" + title.lower().replace("-", "") + ".jpg"))
    report["restored_values"] = restore(device, ui, original)
    require(runtime(device)["errors"] == report["original_runtime"]["errors"], "Unexpected JavaScript fault during panel tests")
    require(device.request("DBG_CHECK", timeout=20, attempts=1) == "ok=1", "Heap integrity failed")
    report["controls_verified"] = True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM10")
    parser.add_argument("--baud", type=int, default=921600)
    parser.add_argument("--elf", default=str(ROOT / "build/ouse_core_desktop.elf"))
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    report = dict(status="running", started=time.time(), stages=[], port=args.port, baud=args.baud)
    device, exit_code = None, 0
    def save():
        if device is not None:
            save_json(device.output / "quick-panel-result.json", report)
    try:
        device = Device(args.port, args.output, args.elf, baud=args.baud)
        run(device, args, report, save)
        report["status"] = "passed"
    except (Exception, KeyboardInterrupt) as error:
        report.update(status="interrupted" if isinstance(error, KeyboardInterrupt) else "failed",
                      error=dict(type=type(error).__name__, message=str(error)))
        exit_code = 130 if isinstance(error, KeyboardInterrupt) else 1
        print("Quick panel:", str(error), file=sys.stderr, flush=True)
        if device is not None and healthy(device):
            try:
                report["failure_state"] = device.state()
                save_json(device.output / "failure-tree.json", device.tree())
            except Exception as diagnostic_error:
                report["diagnostic_error"] = str(diagnostic_error)
    finally:
        if device is not None:
            if healthy(device) and report.get("owns_fixture"):
                try:
                    if report.get("original_values") and not report.get("restored_values"):
                        report["restored_values"] = restore(device, UI(device), report["original_values"])
                    if APP_ID in installed_ids(device):
                        device.request("REMOVE", APP_ID, timeout=30, attempts=1)
                    require(installed_ids(device) == report["original_ids"], "Cleanup changed other installed packages")
                    initial = report["original_state"]
                    device.request("DBG_HOME")
                    if initial["active"]:
                        device.request("DBG_OPEN", initial["active"], timeout=10)
                    final = wait_for(device.state, lambda s: s["active"] == initial["active"] and preferences(s) == preferences(initial), "Initial foreground/preferences were not restored")
                    initial_runtime = report["original_runtime"]
                    final_runtime = wait_for(lambda: runtime(device),
                        lambda s: all(s[key] == initial_runtime[key] for key in ("contexts", "instances", "storage_jobs", "storage_pending")),
                        "Temporary script context or storage requests were not released")
                    report.update(final_state=final, final_runtime=final_runtime, cleanup_completed=True)
                except Exception as cleanup_error:
                    report.update(cleanup_error=str(cleanup_error), status="failed")
                    exit_code = 1
            save()
            try:
                device.close()
            except Exception as close_error:
                report.update(close_error=str(close_error), status="failed")
                exit_code = 1
            report.update(finished=time.time(), panic_seen=bool(device.capture.panic), unexpected_reboot=device.rebooted,
                          serial_closed=not bool(device.port and device.port.is_open))
            if report["panic_seen"] or report["unexpected_reboot"]:
                report["status"], exit_code = "failed", 1
            save()
            print("Evidence:", device.output, flush=True)
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
