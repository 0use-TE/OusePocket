"""Exercise the real script worker, background policy and system keyboard.

Uses only user.framework-test, refuses to replace an existing package of that
name, and preserves every other package and preference. No host simulation,
unit-test campaign, flash, erase, reset or network access is performed.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
import shutil
import sys
import time

import bpk
from device import Device


APP_ID = "user.framework-test"
ROOT = Path(__file__).resolve().parents[1]
INPUT_TITLES = ("Text · Resize", "Number · Pan", "Password · Resize", "Text · Overlay")
DROPDOWN_OPTIONS = "First\nSecond\nThird"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def healthy(device):
    return not device.capture.panic and not device.rebooted and device.error is None


def integers(device, verb):
    return {key: int(value) for key, value in
            (item.split("=", 1) for item in device.request(verb).split(","))}


def wait_contexts(device, expected):
    deadline = time.monotonic() + 6
    while True:
        stats = integers(device, "DBG_RUNTIME")
        if stats["contexts"] == expected:
            return stats
        require(time.monotonic() < deadline, "Worker did not release application context: " + repr(stats))
        time.sleep(.08)


def installed_ids(device):
    text = device.request("LIST", timeout=8)
    return [] if text == "-" else sorted(text.split(","))


def upload(device, data):
    device.request("BEGIN", f"{len(data):08x}:{bpk.crc(data):08x}", timeout=8)
    try:
        for offset in range(0, len(data), 512):
            chunk = data[offset:offset + 512]
            reply = device.request("DATA", f"{offset:08x}:{chunk.hex()}", timeout=8)
            require(int(reply) == offset + len(chunk), "Wrong upload offset acknowledged")
        require(device.request("END", timeout=20) == APP_ID, "Wrong installed application ID")
    except BaseException:
        if healthy(device):
            try:
                device.request("ABORT", attempts=1)
            except Exception:
                pass
        raise


def packages(output, faults):
    result = {}
    for name in ("baseline", "replacement", "denied", "resume-timer", "begin-on-suspend", *(("fault",) if faults else ())):
        source = output / name
        shutil.copytree(ROOT / "apps/framework-test", source)
        if name == "denied":
            manifest = json.loads((source / "manifest.json").read_text(encoding="utf-8"))
            manifest["capabilities"] = {}
            (source / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
        if name == "replacement":
            page = json.loads((source / "page.json").read_text(encoding="utf-8"))
            page["root"]["children"][0]["text"] = "Framework Replacement"
            (source / "page.json").write_text(json.dumps(page), encoding="utf-8")
        if name == "fault":
            with (source / "app.js").open("a", encoding="utf-8") as file:
                file.write("\nOuse.ui.on('begin','click',()=>{while(true){}});\n")
        if name in ("resume-timer", "begin-on-suspend"):
            with (source / "app.js").open("a", encoding="utf-8") as file:
                file.write("\n" + ("resumeProbe" if name == "resume-timer" else "beginOnSuspend") + " = true;\n")
        archive = output / (name + ".bpk")
        require(bpk.pack(source, archive)["id"] == APP_ID, "Unexpected fixture ID")
        result[name] = archive.read_bytes()
    return result


class UI:
    def __init__(self, device):
        self.device = device
        self.resume_epoch = -1
        self.last_tree = None
        state = device.state()
        self.width, self.height, self.dpi = state["width"], state["height"], state["dpi"]
        require(self.width >= 240 and self.height >= 320, "Unsupported display geometry")

    def dp(self, value):
        return round(value * self.dpi / 160)

    def tree(self):
        tree = self.device.tree()
        self.last_tree = tree
        (self.device.output / "last-tree.json").write_text(
            json.dumps(tree, ensure_ascii=False, indent=2), encoding="utf-8")
        stack = []
        for node in tree["nodes"]:
            while stack and stack[-1]["depth"] >= node["depth"]:
                stack.pop()
            node["_parents"] = tuple(stack)
            stack.append(node)
        return tree

    def clipped(self, node):
        bounds = [self.dp(24), self.dp(40), self.width - self.dp(24), self.height - self.dp(32)]
        for item in (*node["_parents"], node):
            x1, y1, x2, y2 = item["bounds"]
            bounds = [max(bounds[0], x1), max(bounds[1], y1), min(bounds[2], x2), min(bounds[3], y2)]
        return bounds if bounds[2] - bounds[0] > 4 and bounds[3] - bounds[1] > 10 else None

    def scroll(self, forward):
        y1 = round(self.height * (.62 if forward else .32))
        y2 = round(self.height * (.32 if forward else .68))
        self.device.swipe(self.width - self.dp(28), y1, self.width - self.dp(28), y2, 180, hold_ms=160)
        # A UART tree is a snapshot from before its download. Wait for actual
        # inertia/elastic scrolling to settle before using its coordinates.
        previous, stable = None, 0
        deadline = time.monotonic() + 8
        while True:
            signature = [(node["type"], node["bounds"]) for node in self.tree()["nodes"]]
            stable = stable + 1 if signature == previous else 0
            if stable >= 2:
                return
            require(time.monotonic() < deadline, "Scroll geometry did not settle")
            previous = signature
            time.sleep(.08)

    def label(self, text, prefix=False):
        for forward in (False, True):
            previous, unchanged = None, 0
            for _ in range(12):
                tree = self.tree()
                found = [node for node in tree["nodes"] if node["type"] in ("label", "checkbox") and
                         (node["text"].startswith(text) if prefix else node["text"] == text)]
                require(len(found) <= 1, "Ambiguous fixture label: " + text)
                if found and self.clipped(found[0]):
                    return found[0], tree
                signature = [(node["type"], node["bounds"]) for node in tree["nodes"]]
                unchanged = unchanged + 1 if signature == previous else 0
                if unchanged >= 2:
                    break
                previous = signature
                self.scroll(forward)
        raise RuntimeError("Fixture label could not be brought into view: " + text)

    def tap_label(self, text):
        node, _ = self.label(text)
        bounds = self.clipped(node)
        return self.device.tap((bounds[0] + bounds[2]) // 2, (bounds[1] + bounds[3]) // 2)

    def text(self, prefix, tree=None):
        nodes = (tree or self.tree())["nodes"]
        texts = [node["text"] for node in nodes if node["type"] == "label" and node["text"].startswith(prefix)]
        require(len(texts) <= 1, "Ambiguous fixture state: " + prefix)
        return texts[0] if texts else self.label(prefix, prefix=True)[0]["text"]

    def open(self, force_new=False):
        already_active = self.device.state()["active"] == APP_ID
        if force_new:
            self.resume_epoch = -1
        expected = max(0, self.resume_epoch + (0 if already_active else 1))
        self.device.request("OPEN", APP_ID, timeout=10)
        deadline = time.monotonic() + 8
        while True:
            tree = self.tree()
            if not any(node["type"] == "label" and node["text"].startswith("state=") for node in tree["nodes"]):
                _, tree = self.label("state=", prefix=True)
            state_ready = any(node["type"] == "label" and node["text"].startswith("state=foreground;") for node in tree["nodes"])
            cycles = [node["text"] for node in tree["nodes"] if node["type"] == "label" and node["text"].startswith("resumeEpoch=")]
            if state_ready and cycles:
                epoch = int(cycles[0].split(";", 1)[0].split("=", 1)[1])
                if epoch >= expected:
                    self.resume_epoch = epoch
                    return tree
            require(time.monotonic() < deadline, "Asynchronous worker did not start application")
            time.sleep(.1)

    def gap(self, tree=None):
        return int(self.text("resumeEpoch=", tree).split(";resumeGap=", 1)[1])

    def wait_text(self, prefix, expected):
        deadline = time.monotonic() + 5
        while True:
            value = self.text(prefix)
            if value == expected:
                return value
            require(time.monotonic() < deadline, "Worker state did not apply: " + repr((expected, value)))
            time.sleep(.08)

    def input(self, index, focus=False):
        self.label(INPUT_TITLES[index])
        for _ in range(12):
            nodes = self.tree()["nodes"]
            titles = [node for node in nodes if node["type"] == "label" and node["text"] == INPUT_TITLES[index]]
            if titles:
                inputs = [node for node in nodes if node["type"] == "input" and node["bounds"][1] > titles[0]["bounds"][1]]
                if inputs:
                    node = min(inputs, key=lambda item: item["bounds"][1])
                    clipped = self.clipped(node)
                    if clipped and clipped[3] - clipped[1] >= self.dp(24):
                        if focus:
                            self.device.tap((clipped[0] + clipped[2]) // 2, (clipped[1] + clipped[3]) // 2)
                        return node
            self.scroll(True)
        raise RuntimeError("Input could not be brought into view")

    def dropdown(self):
        for _ in range(12):
            nodes = [node for node in self.tree()["nodes"] if node["type"] == "dropdown"]
            require(len(nodes) <= 1, "Fixture dropdown ambiguous")
            if nodes:
                bounds = self.clipped(nodes[0])
                if bounds and bounds[3] - bounds[1] >= self.dp(24):
                    return nodes[0]
            self.scroll(True)
        raise RuntimeError("Dropdown could not be brought into view")


def run(device, args, report, save):
    hello = device.request("HELLO", timeout=8, attempts=3)
    require("storage=ready" in hello and "debug=1" in hello, "Storage/debug service unavailable")
    actual_elf = device.request("DBG_BUILD")
    elf_path = Path(args.elf) if args.elf else ROOT / "build/ouse_core_desktop.elf"
    expected_elf = hashlib.sha256(elf_path.read_bytes()).hexdigest()
    require(actual_elf == expected_elf, "Running firmware does not match supplied ELF")
    baseline = installed_ids(device)
    require(APP_ID not in baseline, APP_ID + " already exists; refusing to overwrite or remove it")
    report.update(hello=hello, elf_sha256=actual_elf, initial_ids=baseline, owns_test_id=True, initial_state=device.state(),
                  initial_runtime=integers(device, "DBG_RUNTIME"))
    data = packages(device.output / "packages", args.faults)
    report["packages"] = {key: {"bytes": len(value), "sha256": hashlib.sha256(value).hexdigest()}
                          for key, value in data.items()}
    save()

    def record(stage, **details):
        require(healthy(device), "Board or UART failed")
        require(installed_ids(device) == sorted(baseline + [APP_ID]), "Other installed packages changed")
        report["stages"].append(dict(stage=stage, time=time.time(), runtime=integers(device, "DBG_RUNTIME"), **details))
        if ui.last_tree:
            (device.output / (f"stage-{len(report['stages']):02d}-{stage}-tree.json")).write_text(
                json.dumps({"nodes": [{k: v for k, v in node.items() if k != "_parents"} for node in ui.last_tree["nodes"]]},
                           ensure_ascii=False, indent=2), encoding="utf-8")
        save()
        print("Real-board framework:", stage, "passed", flush=True)

    device.request("DBG_HOME")
    upload(device, data["baseline"])
    ui = UI(device)
    ui.open()
    active_epoch = ui.resume_epoch
    ui.open()
    require(ui.resume_epoch == active_epoch, "Opening the foreground app invoked onResume again")
    require("background=false" in ui.text("state="), "Background work unexpectedly active at launch")
    for index in range(args.rounds):
        device.request("DBG_HOME")
        time.sleep(.7)
        default_gap = ui.gap(ui.open())
        require(default_gap <= 1, "Default application executed timers while suspended")
        ui.tap_label("Run in background")
        require(ui.text("result=") == "result=begin:true", "Background work was not accepted")
        device.request("DBG_HOME")
        time.sleep(.8)
        gap = ui.gap(ui.open())
        require(gap >= 4, "Eligible application did not continue background timers")
        ui.tap_label("Stop background")
        ui.wait_text("result=", "result=end:false")
        ui.wait_text("state=", "state=foreground;background=false")
        device.request("DBG_HOME")
        time.sleep(.7)
        ended_gap = ui.gap(ui.open())
        require(ended_gap <= 1, "background.end did not suspend hidden application")
        record("background_round", round=index + 1, default_gap=default_gap, runnable_gap=gap,
               ended_gap=ended_gap, resume_epoch=ui.resume_epoch)

    ui.tap_label("Stop after 1 second")
    device.request("DBG_HOME")
    time.sleep(1.4)
    ui.open()
    require(ui.text("result=") == "result=ended-in-background", "Hidden app could not end its background work")
    require("background=false" in ui.text("state="), "Hidden end left background work active")
    ui.tap_label("Try Home after 1 second")
    device.request("DBG_HOME")
    device.request("DBG_OPEN", "ouse.settings", timeout=10)
    time.sleep(1.3)
    require(device.state()["active"] == "ouse.settings", "Background script displaced the foreground application")
    ui.open()
    require(ui.text("result=") == "result=background-home:false", "Background Home was not explicitly rejected")
    ui.tap_label("Stop background")
    record("hidden_end_and_navigation_ownership")

    # Use the real switch hit region; its event is delivered by the worker.
    for _ in range(10):
        nodes = ui.tree()["nodes"]
        switches = [n for n in nodes if n["type"] == "switch"]
        if not switches:
            lower_controls = any(n["type"] == "input" or n["text"].startswith("input=") for n in nodes)
            ui.scroll(not lower_controls)
            continue
        require(len(switches) == 1, "Fixture switch ambiguous")
        bounds = ui.clipped(switches[0])
        if bounds:
            device.tap((bounds[0] + bounds[2]) // 2, (bounds[1] + bounds[3]) // 2)
            break
        ui.scroll(switches[0]["bounds"][1] > ui.height // 2)
    require(ui.text("result=") == "result=switch:true", "Native control event did not reach JS")
    ui.tap_label("Enable option")
    require(ui.text("result=") == "result=checkbox:true", "Checkbox event did not reach JS")
    record("native_control_events")

    # LVGL puts dropdown lists on its global top layer. The application's
    # native pause path must close that popup before hiding its own viewport.
    # Detect the actual visible option list, without a synthetic JS close.
    def dropdown_popup_visible(tree):
        return any(node["type"] == "label" and node["text"] == DROPDOWN_OPTIONS for node in tree["nodes"])

    dropdown = ui.dropdown()
    selected_before = dropdown["text"]
    a, b, c, d = ui.clipped(dropdown)
    device.tap((a + c) // 2, (b + d) // 2)
    deadline = time.monotonic() + 5
    while not dropdown_popup_visible(ui.tree()):
        require(time.monotonic() < deadline, "Real dropdown tap did not expose its option list")
        time.sleep(.08)
    device.request("DBG_HOME")
    require(not device.state()["active"], "Home did not background dropdown owner")
    require(not dropdown_popup_visible(ui.tree()), "Dropdown top-layer popup survived Home")
    ui.open()
    require(not dropdown_popup_visible(ui.tree()), "Dropdown reopened when its application resumed")
    selected_after = ui.dropdown()["text"]
    require(selected_after == selected_before, "Backgrounding changed the dropdown selection")
    record("dropdown_home_closes_popup", selected_before=selected_before, selected_after=selected_after)

    ime_cases = ((0, 0, 0, "resizeInput"), (1, 1, 1, "numberInput"),
                 (2, 0, 2, "secretInput"), (3, 2, 0, "overlayInput"))
    for index, mode, keyboard_type, input_id in ime_cases:
        before = ui.input(index, focus=True)
        original = before["_parents"][-1]["bounds"]
        ime = integers(device, "DBG_IME")
        require(ime["visible"] == 1 and ime["mode"] == mode and ime["keyboard_type"] == keyboard_type,
                "Wrong keyboard mode/type: " + repr(ime))
        if mode != 2:
            require(ime["input_y1"] >= ime["viewport_y1"] and ime["input_y2"] < ime["keyboard_y1"],
                    "Keyboard covers the focused input: " + repr(ime))
        if mode == 0:
            require(ime["viewport_height"] < original[3] - original[1] + 1, "Resize did not reduce viewport")
        else:
            require(ime["viewport_height"] == original[3] - original[1] + 1, "Pan/overlay changed viewport size")
        if mode == 2:
            require(ime["viewport_y1"] == original[1], "Overlay unexpectedly shifted viewport")
        # Pinning LVGL 9.5 fixes these default key-map hit regions. Key positions
        # come from actual keyboard geometry, not a synthetic text injection.
        key_x = round(ui.width * (.10 if keyboard_type == 1 else .14))
        key_y = ime["keyboard_y1"] + max(8, (ime["keyboard_y2"] - ime["keyboard_y1"]) // 8)
        device.tap(key_x, key_y)
        tree = ui.tree()
        require(all(not node["text"] for node in tree["nodes"] if node["type"] == "input"),
                "Debug tree disclosed input contents")
        # Bound screenshot transfers; the input/keyboard are native screen
        # children, so a real snapshot includes both.
        if index in (0, 1, 3):
            device.screenshot(device.output / (input_id + ".jpg"))
        (device.output / (input_id + "-tree.json")).write_text(
            json.dumps({"nodes": [{k: v for k, v in node.items() if k != "_parents"} for node in tree["nodes"]]},
                       ensure_ascii=False, indent=2), encoding="utf-8")
        state = device.swipe(2, ui.dp(105), ui.width // 2, ui.dp(105), 180)
        require(state["active"] == APP_ID and integers(device, "DBG_IME")["visible"] == 0,
                "Back did not hide keyboard before navigating")
        restored_inputs = [node for node in ui.tree()["nodes"] if node["type"] == "input" and node["bounds"] == before["bounds"]]
        require(len(restored_inputs) == 1, "Keyboard dismissal did not restore input scroll position")
        restored = restored_inputs[0]
        require(restored["_parents"][-1]["bounds"] == original, "Keyboard dismissal did not restore viewport geometry")
        require(restored["bounds"] == before["bounds"], "Keyboard dismissal did not restore scroll position")
        status = ui.text("input=")
        expected = "1" if keyboard_type == 1 else "q"
        require(status == ("input=secretInput:length=1" if keyboard_type == 2 else "input=" + input_id + ":" + expected),
                "Real keyboard did not deliver expected text: " + status)
        record("keyboard_" + input_id, keyboard=ime, status=status)

    ui.tap_label("Run in background")
    attached_before_close = integers(device, "DBG_IME")["attachments"]
    device.request("DBG_HOME")
    device.request("DBG_CLOSE", APP_ID, timeout=10)
    wait_contexts(device, report["initial_runtime"]["contexts"])
    require(integers(device, "DBG_IME")["attachments"] == attached_before_close - 4,
            "Explicit close retained input bindings")
    tree = ui.open(force_new=True)
    require(ui.gap(tree) == 0 and "background=false" in ui.text("state=", tree), "Close did not create a fresh session")
    record("explicit_close_releases_context")

    # Replace the foreground package while its keyboard owns an input.
    ui.input(0, focus=True)
    attached_before_update = integers(device, "DBG_IME")["attachments"]
    upload(device, data["replacement"])
    update_ime = integers(device, "DBG_IME")
    require(device.state()["active"] == "" and update_ime["visible"] == 0,
            "Update retained old foreground or keyboard ownership")
    require(update_ime["attachments"] == attached_before_update - 4, "Update retained old input bindings")
    tree = ui.open(force_new=True)
    require(any(n["text"] == "Framework Replacement" for n in tree["nodes"]), "Replacement page was not opened")
    require("background=false" in ui.text("state=", tree), "Update retained old background state")
    record("live_update_teardown")

    upload(device, data["denied"])
    ui.open(force_new=True)
    ui.tap_label("Run in background")
    require(ui.text("result=") == "result=begin:false", "Missing background capability was accepted")
    record("background_capability_enforced")

    upload(device, data["resume-timer"])
    ui.open(force_new=True)
    device.request("DBG_HOME")
    time.sleep(.7)
    raw_offset = device.capture.bytes
    ui.open()
    deadline = time.monotonic() + 3
    while True:
        with (device.output / "uart.raw.log").open("rb") as raw:
            raw.seek(raw_offset)
            match = re.search(rb"resume-timer:(\d+)", raw.read())
        if match:
            elapsed = int(match[1])
            break
        require(time.monotonic() < deadline, "New onResume timer never executed")
        time.sleep(.05)
    require(60 <= elapsed <= 450, "New onResume timer inherited the previous suspension: " + str(elapsed))
    record("new_timer_uses_resume_clock", callback_ms=elapsed, suspended_ms=700)

    upload(device, data["begin-on-suspend"])
    ui.open(force_new=True)
    device.request("DBG_HOME")
    time.sleep(.8)
    hidden_runtime = integers(device, "DBG_RUNTIME")
    require(hidden_runtime["background"] >= 1, "Lifecycle callback background.begin did not reactivate worker")
    gap = ui.gap(ui.open())
    require(gap >= 4, "Lifecycle background.begin did not execute hidden timers")
    ui.wait_text("result=", "result=begin-on-suspend:true")
    record("lifecycle_can_begin_background", runnable_gap=gap, hidden_runtime=hidden_runtime)

    if args.faults:
        upload(device, data["fault"])
        ui.open(force_new=True)
        started = time.monotonic()
        ui.tap_label("Run in background")
        require(time.monotonic() - started < 8, "JS interrupt blocked native UI/transport")
        fault_tree = ui.tree()
        require(any(n["text"] in ("应用运行出错", "Application error") for n in fault_tree["nodes"]),
                "JS infinite loop did not produce native error panel")
        device.request("DBG_OPEN", "ouse.settings", timeout=5)
        require(device.state()["active"] == "ouse.settings", "JS failure blocked native Settings")
        upload(device, data["baseline"])
        tree = ui.open(force_new=True)
        require(ui.text("ticks=", tree).startswith("ticks="), "Worker did not recover after interrupted script")
        record("infinite_loop_isolated")

    ui.input(3, focus=True)
    attached_before_remove = integers(device, "DBG_IME")["attachments"]
    device.request("REMOVE", APP_ID, timeout=10)
    require(installed_ids(device) == baseline, "Removal changed other installed packages")
    final_ime = integers(device, "DBG_IME")
    require(final_ime["visible"] == 0, "Removal retained keyboard focus")
    require(final_ime["attachments"] == attached_before_remove - 4, "Removal retained input bindings")
    final_runtime = wait_contexts(device, report["initial_runtime"]["contexts"])
    report.update(final_runtime=final_runtime, final_ime=final_ime, cleanup_completed=True)
    save()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM10")
    parser.add_argument("--elf")
    parser.add_argument("--output")
    parser.add_argument("--rounds", type=int, choices=range(1, 4), default=1)
    parser.add_argument("--faults", action="store_true", help="Also trigger one bounded infinite-loop JS fault")
    args = parser.parse_args()
    report = dict(started=time.time(), status="running", stages=[], rounds=args.rounds, faults=args.faults)
    device = None
    exit_code = 0

    def save():
        if device is not None:
            (device.output / "framework-result.json").write_text(
                json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")

    try:
        device = Device(args.port, args.output, args.elf)
        save()
        run(device, args, report, save)
        report["status"] = "passed"
    except (Exception, KeyboardInterrupt) as error:
        report.update(status="interrupted" if isinstance(error, KeyboardInterrupt) else "failed",
                      error={"type": type(error).__name__, "message": str(error)})
        print("Framework regression:", report["status"], error, file=sys.stderr, flush=True)
        if device is not None and healthy(device):
            try:
                report.update(failure_state=device.state(), failure_runtime=integers(device, "DBG_RUNTIME"),
                              failure_ime=integers(device, "DBG_IME"))
                (device.output / "failure-tree.json").write_text(
                    json.dumps(device.tree(), ensure_ascii=False, indent=2), encoding="utf-8")
            except Exception as diagnostic_error:
                report["failure_diagnostic_error"] = str(diagnostic_error)
        exit_code = 130 if isinstance(error, KeyboardInterrupt) else 1
    finally:
        if device is not None:
            if report.get("owns_test_id") and healthy(device):
                try:
                    if APP_ID in installed_ids(device):
                        device.request("REMOVE", APP_ID, timeout=10)
                    require(installed_ids(device) == report["initial_ids"], "Final cleanup changed installed packages")
                    original = report["initial_state"]["active"]
                    device.request("DBG_OPEN", original, timeout=10) if original else device.request("DBG_HOME")
                    report["cleanup_completed"] = True
                except Exception as error:
                    report["cleanup_error"] = str(error)
                    report["status"], exit_code = "failed", 1
            save()
            try:
                device.close()
            except Exception as error:
                report["capture_close_error"] = str(error)
                report["status"], exit_code = "failed", 1
            report.update(panic_seen=bool(device.capture.panic), unexpected_reboot=device.rebooted, finished=time.time())
            if report["panic_seen"] or report["unexpected_reboot"]:
                report["status"], exit_code = "failed", 1
            save()
            print("Evidence:", device.output.resolve(), flush=True)
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
