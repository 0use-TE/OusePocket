"""Short real-board visual check of native controls in a temporary BPK.

Save five actual LCD screenshots for human inspection, exercise real touch
events, restore appearance and remove only this invocation's package. Never
build, flash, erase, reset, or run a host UI simulation.
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
APP_ID = "user.controls-test"
OPTIONS = "Alpha\nBeta\nGamma"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def healthy(device):
    return not device.capture.panic and not device.rebooted and device.error is None


def wait_for(probe, predicate, message, timeout=8):
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


def fixture(directory, original):
    directory.mkdir()
    manifest = dict(runtime="ouse-core", api=1, id=APP_ID, name="Control Visual Test", icon="icon.bin")
    # Coordinates are in the existing 320 x 416 dp application viewport. All
    # controls fit simultaneously; normalizing the font is restored on exit.
    def node(kind, ident, x, y, width, height, **properties):
        return dict(type=kind, id=ident, x=x, y=y, width=width, height=height, **properties)
    page = {"api": 1, "units": "dp", "root": {
        "type": "container", "id": "page", "width": "fill", "height": "fill", "children": [
            node("label", "title", 12, 8, 296, 26, text="Native controls", fontSize=20),
            node("label", "status", 12, 38, 296, 40, text="ready=0", fontSize=12),
            node("label", "switchLabel", 12, 90, 90, 26, text="Switch", fontSize=16),
            node("switch", "switch", 112, 84, 52, 32, value=False),
            node("checkbox", "checkbox", 190, 84, 116, 36, value=False, text="Check", fontSize=16),
            node("label", "sliderLabel", 12, 134, 64, 24, text="Slider", fontSize=14),
            node("slider", "slider", 86, 135, 212, 20, min=0, max=100, value=45),
            node("progressBar", "progress", 12, 176, 286, 10, min=0, max=100, value=45),
            node("dropdown", "dropdown", 12, 204, 286, 40, options=["Alpha", "Beta", "Gamma"], value=0),
            node("arc", "arc", 24, 264, 94, 94, min=0, max=100, value=65),
            node("spinner", "spinner", 192, 282, 54, 54),
            node("button", "light", 12, 370, 90, 40, text="Light", fontSize=14),
            node("button", "dark", 114, 370, 90, 40, text="Dark", fontSize=14),
            node("button", "restore", 216, 370, 90, 40, text="Restore", fontSize=14)
        ]}}
    restore_theme = "dark" if original["theme"] else "light"
    script = """'use strict';
let se=0,ce=0,ve=0;
function render(){Ouse.ui.setText('status','ready=1;s='+Ouse.ui.getValue('switch')+';c='+Ouse.ui.getValue('checkbox')+';se='+se+';ce='+ce+';v='+Ouse.ui.getValue('slider')+';ve='+ve+';t='+Ouse.theme.get().mode);}
Ouse.ui.on('switch','change',()=>{++se;render();});
Ouse.ui.on('checkbox','change',()=>{++ce;render();});
Ouse.ui.on('slider','change',value=>{++ve;Ouse.ui.setValue('progress',value);render();});
Ouse.ui.on('light','click',()=>{Ouse.theme.set('light');render();});
Ouse.ui.on('dark','click',()=>{Ouse.theme.set('dark');render();});
Ouse.ui.on('restore','click',()=>{Ouse.theme.set(RESTORE_THEME);Ouse.display.setFontPercent(RESTORE_FONT);render();});
Ouse.theme.onChange(render);
Ouse.display.onChange(render);
globalThis.app={onResume:render};
Ouse.theme.set('light');Ouse.display.setFontPercent(100);render();
""".replace("RESTORE_THEME", json.dumps(restore_theme)).replace("RESTORE_FONT", str(original["font"]))
    (directory / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
    (directory / "page.json").write_text(json.dumps(page), encoding="utf-8")
    (directory / "app.js").write_text(script, encoding="utf-8")
    (directory / "icon.bin").write_bytes(b"OIC1" + struct.pack("<HH", 16, 16) + bytes((0x70, 0xA0, 0x40, 0xFF)) * 256)
    archive = directory.with_suffix(".bpk")
    require(bpk.pack(directory, archive)["id"] == APP_ID, "Unexpected local fixture ID")
    return archive.read_bytes()


def upload(device, data):
    device.request("BEGIN", f"{len(data):08x}:{bpk.crc(data):08x}", timeout=8)
    try:
        for offset in range(0, len(data), 512):
            chunk = data[offset:offset + 512]
            reply = device.request("DATA", f"{offset:08x}:{chunk.hex()}", timeout=8)
            require(int(reply) == offset + len(chunk), "Wrong upload offset acknowledged")
        require(device.request("END", timeout=20) == APP_ID, "Wrong installed fixture ID")
    except BaseException:
        if healthy(device):
            try:
                device.request("ABORT", attempts=1)
            except Exception:
                pass
        raise


class UI:
    def __init__(self, device):
        self.device = device

    def tree(self):
        tree = self.device.tree()
        (self.device.output / "last-tree.json").write_text(json.dumps(tree, ensure_ascii=False, indent=2), encoding="utf-8")
        return tree

    def status(self):
        values = [node["text"] for node in self.tree()["nodes"] if node["text"].startswith("ready=1;")]
        require(len(values) <= 1, "Fixture status ambiguous")
        return dict(item.split("=", 1) for item in values[0].split(";")) if values else None

    def control(self, kind):
        nodes = [node for node in self.tree()["nodes"] if node["type"] == kind]
        require(len(nodes) == 1, "Visible fixture control not unique: " + kind)
        return nodes[0]

    def tap_node(self, node):
        a, b, c, d = node["bounds"]
        return self.device.tap((a + c) // 2, (b + d) // 2)

    def tap(self, text):
        nodes = [node for node in self.tree()["nodes"] if node["text"] == text]
        require(len(nodes) == 1, "Fixture caption not unique: " + text)
        return self.tap_node(nodes[0])

    def wait_status(self, predicate, description):
        return wait_for(self.status, lambda status: status is not None and predicate(status), description)

    def theme(self, value):
        self.tap("Dark" if value else "Light")
        wait_for(self.device.state, lambda state: state["theme"] == value, "Theme request not applied")
        self.wait_status(lambda status: status["t"] == ("dark" if value else "light"), "JS theme snapshot not updated")

    def restore(self, original):
        self.device.request("OPEN", APP_ID, timeout=10)
        wait_for(self.device.state, lambda state: state["active"] == APP_ID, "Cannot open fixture to restore appearance")
        # Restore's location is fixed and remains accessible if a preceding
        # visual assertion failed before the first tree could be downloaded.
        self.tap("Restore")
        wait_for(self.device.state, lambda state: all(state[key] == original[key] for key in ("theme", "font", "locale")),
                 "Appearance restoration did not apply")


def run(device, args, report, save):
    hello = device.request("HELLO", timeout=8, attempts=3)
    require("storage=ready" in hello and "debug=1" in hello, "Storage/debug service unavailable")
    actual = device.request("DBG_BUILD")
    elf = Path(args.elf) if args.elf else ROOT / "build/ouse_core_desktop.elf"
    require(actual == hashlib.sha256(elf.read_bytes()).hexdigest(), "Running firmware does not match supplied ELF")
    original = device.state()
    require(original["width"] * 160 // original["dpi"] >= 320 and original["height"] * 160 // original["dpi"] >= 480,
            "Visual fixture needs a 320 x 480 dp display")
    initial_ids = installed_ids(device)
    require(APP_ID not in initial_ids, "Fixture ID exists; refusing overwrite or removal")
    report.update(hello=hello, elf_sha256=actual, initial_ids=initial_ids, initial_state=original,
                  initial_runtime=runtime(device), owns_test_id=True)
    data = fixture(device.output / "package", original)
    report["package"] = dict(bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
    save()
    device.request("DBG_HOME")
    upload(device, data)
    device.request("OPEN", APP_ID, timeout=10)
    wait_for(device.state, lambda state: state["active"] == APP_ID and state["font"] == 100 and state["theme"] == 0,
             "Asynchronous fixture startup not complete")
    ui = UI(device)
    ui.wait_status(lambda state: state["s"] == "false" and state["c"] == "false" and state["t"] == "light", "Initial controls not ready")

    def capture(name):
        require(healthy(device), "Board/UART failed")
        tree = ui.tree()
        for kind in ("switch", "checkbox", "slider", "progressBar", "dropdown", "arc", "spinner"):
            require(any(node["type"] == kind for node in tree["nodes"]), "Control missing from actual visible tree: " + kind)
        (device.output / (name + "-tree.json")).write_text(json.dumps(tree, ensure_ascii=False, indent=2), encoding="utf-8")
        device.screenshot(device.output / (name + ".jpg"))
        report["stages"].append(dict(stage=name, status=ui.status(), state=device.state(), time=time.time()))
        save()
        print("Real-board controls:", name, "captured", flush=True)

    capture("light-unchecked")
    ui.tap_node(ui.control("switch"))
    ui.wait_status(lambda state: state["s"] == "true" and state["se"] == "1", "Real switch tap not delivered")
    ui.tap_node(ui.control("checkbox"))
    ui.wait_status(lambda state: state["c"] == "true" and state["ce"] == "1", "Real checkbox tap not delivered")
    slider = ui.control("slider")
    a, b, c, d = slider["bounds"]
    y = (b + d) // 2
    device.swipe(round(a + (c - a) * .45), y, round(a + (c - a) * .85), y, 180)
    ui.wait_status(lambda state: int(state["ve"]) >= 1 and int(state["v"]) >= 70, "Real slider drag not delivered")
    capture("light-checked")
    ui.theme(1)
    capture("dark-checked")
    ui.tap_node(ui.control("switch"))
    ui.wait_status(lambda state: state["s"] == "false" and state["se"] == "2", "Switch second tap not delivered")
    ui.tap_node(ui.control("checkbox"))
    ui.wait_status(lambda state: state["c"] == "false" and state["ce"] == "2", "Checkbox second tap not delivered")
    capture("dark-unchecked")
    ui.tap_node(ui.control("dropdown"))
    popup = wait_for(ui.tree, lambda tree: any(node["type"] == "label" and node["text"] == OPTIONS for node in tree["nodes"]),
                     "Dropdown tap did not expose actual options")
    (device.output / "dark-dropdown-open-tree.json").write_text(json.dumps(popup, ensure_ascii=False, indent=2), encoding="utf-8")
    device.screenshot(device.output / "dark-dropdown-open.jpg")
    report["stages"].append(dict(stage="dark-dropdown-open", time=time.time(), state=device.state()))
    device.request("DBG_HOME")
    require(not any(node["type"] == "label" and node["text"] == OPTIONS for node in ui.tree()["nodes"]),
            "Dropdown list survived Home")
    require(device.request("DBG_CHECK", timeout=20, attempts=1) == "ok=1", "Heap integrity check failed")
    report["visual_review_required"] = True
    save()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM10")
    parser.add_argument("--elf")
    parser.add_argument("--output")
    args = parser.parse_args()
    report = dict(started=time.time(), status="running", stages=[])
    device, exit_code = None, 0

    def save():
        if device is not None:
            (device.output / "controls-result.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")

    try:
        device = Device(args.port, args.output, args.elf)
        save()
        run(device, args, report, save)
        report["status"] = "passed"
    except (Exception, KeyboardInterrupt) as error:
        report.update(status="interrupted" if isinstance(error, KeyboardInterrupt) else "failed",
                      error={"type": type(error).__name__, "message": str(error)})
        print("Controls real board:", report["status"], error, file=sys.stderr, flush=True)
        if device is not None and healthy(device):
            try:
                report["failure_state"] = device.state()
                (device.output / "failure-tree.json").write_text(json.dumps(device.tree(), ensure_ascii=False, indent=2), encoding="utf-8")
            except Exception as diagnostic_error:
                report["failure_diagnostic_error"] = str(diagnostic_error)
        exit_code = 130 if isinstance(error, KeyboardInterrupt) else 1
    finally:
        if device is not None:
            if report.get("owns_test_id") and healthy(device):
                try:
                    if APP_ID in installed_ids(device):
                        UI(device).restore(report["initial_state"])
                        report["preferences_restored"] = True
                        device.request("REMOVE", APP_ID, timeout=10)
                    require(installed_ids(device) == report["initial_ids"], "Cleanup changed other packages")
                    original = report["initial_state"]["active"]
                    device.request("DBG_OPEN", original, timeout=10) if original else device.request("DBG_HOME")
                    final_runtime = wait_for(lambda: runtime(device), lambda state: state["contexts"] == report["initial_runtime"]["contexts"],
                                             "Fixture context not released")
                    report.update(cleanup_completed=True, final_state=device.state(), final_runtime=final_runtime)
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
