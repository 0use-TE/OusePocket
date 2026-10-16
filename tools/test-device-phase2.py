"""Short real-board check of stage-2 widgets, notifications and top gestures.

One Device owns UART and raw evidence. Only two previously absent fixture IDs
are installed/removed; user settings and other packages are preserved. This
script never builds, flashes, erases, resets or starts a host UI simulation.
"""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import sys
import time

import bpk
from device import Device


ROOT = Path(__file__).resolve().parents[1]
APP_ID = "user.stage2-test"
OTHER_ID = "user.stage2-other"
OWN_IDS = (APP_ID, OTHER_ID)


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def healthy(device):
    return not device.capture.panic and not device.rebooted and device.error is None


def installed_ids(device):
    value = device.request("LIST", timeout=8)
    return [] if value == "-" else sorted(value.split(","))


def stats(device):
    return {key: int(value) for key, value in
            (item.split("=", 1) for item in device.request("DBG_RUNTIME").split(","))}


def notices(device):
    return json.loads(device.resource("DBG_NOTICES").decode("utf-8"))


def wait_for(probe, predicate, message, timeout=8):
    deadline = time.monotonic() + timeout
    while True:
        value = probe()
        if predicate(value):
            return value
        require(time.monotonic() < deadline, message + ": " + repr(value))
        time.sleep(.08)


def upload(device, data, expected):
    device.request("BEGIN", f"{len(data):08x}:{bpk.crc(data):08x}", timeout=8)
    try:
        for offset in range(0, len(data), 512):
            chunk = data[offset:offset + 512]
            acknowledged = device.request("DATA", f"{offset:08x}:{chunk.hex()}", timeout=8)
            require(int(acknowledged) == offset + len(chunk), "Wrong upload offset acknowledged")
        require(device.request("END", timeout=20) == expected, "Wrong installed application ID")
    except BaseException:
        if healthy(device):
            try:
                device.request("ABORT", attempts=1)
            except Exception:
                pass
        raise


def build_fixtures(output):
    """Package local sources; no compilation or board actions here."""
    output.mkdir(parents=True)
    main_source = output / "main"
    shutil.copytree(ROOT / "apps/framework-stage2", main_source)
    other_source = output / "other"
    other_source.mkdir()
    manifest = dict(runtime="ouse-core", api=1, id=OTHER_ID, name="Stage 2 Other", icon="icon.bin",
                    capabilities={"notifications": True})
    page = {"api": 1, "units": "dp", "root": {
        "type": "container", "id": "page", "width": "fill", "height": "fill",
        "layout": "column", "padding": 16, "gap": 12, "children": [
            {"type": "label", "id": "ready", "text": "other=starting", "width": "fill", "height": "content"},
            {"type": "label", "id": "result", "text": "other-result=ready", "width": "fill", "height": "content"},
            {"type": "button", "id": "publish", "text": "Other publish", "width": "fill", "height": 44},
            {"type": "button", "id": "cancel", "text": "Other cancel", "width": "fill", "height": 44},
            {"type": "button", "id": "inspect", "text": "Other inspect", "width": "fill", "height": 44}
        ]}}
    script = """'use strict';
function result(text){Ouse.ui.setText('result','other-result='+text);}
Ouse.ui.on('publish','click',()=>result('publish:'+Ouse.notifications.publish({id:'ordinary',title:'Stage2 other',body:'Separate owner',payload:'other'})));
Ouse.ui.on('cancel','click',()=>result('cancel:'+Ouse.notifications.cancel('ordinary')));
Ouse.ui.on('inspect','click',()=>result('own:'+Ouse.notifications.list().map(x=>x.id).sort().join(',')));
globalThis.app={onResume(){Ouse.ui.setText('ready','other=ready');}};
Ouse.ui.setText('ready','other=ready');
"""
    (other_source / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
    (other_source / "page.json").write_text(json.dumps(page), encoding="utf-8")
    (other_source / "app.js").write_text(script, encoding="utf-8")
    shutil.copy2(main_source / "icon.bin", other_source / "icon.bin")
    result = {}
    for app_id, source in ((APP_ID, main_source), (OTHER_ID, other_source)):
        archive = output / (app_id + ".bpk")
        require(bpk.pack(source, archive)["id"] == app_id, "Wrong local fixture ID")
        result[app_id] = archive.read_bytes()
    return result


class UI:
    def __init__(self, device):
        self.device = device
        self.last_tree = None
        self.resume_epoch = 0
        state = device.state()
        self.width, self.height, self.dpi = state["width"], state["height"], state["dpi"]

    def dp(self, value):
        return round(value * self.dpi / 160)

    def tree(self):
        tree = self.device.tree()
        self.last_tree = tree
        (self.device.output / "last-tree.json").write_text(json.dumps(tree, ensure_ascii=False, indent=2), encoding="utf-8")
        stack = []
        for node in tree["nodes"]:
            while stack and stack[-1]["depth"] >= node["depth"]:
                stack.pop()
            node["_parents"] = tuple(stack)
            stack.append(node)
        return tree

    def clipped(self, node, panel=False):
        bounds = [self.dp(8), self.dp(30), self.width - self.dp(8), self.height - self.dp(28)]
        if not panel:
            bounds = [self.dp(24), self.dp(40), self.width - self.dp(24), self.height - self.dp(32)]
        for parent in (*node.get("_parents", ()), node):
            a, b, c, d = parent["bounds"]
            bounds = [max(bounds[0], a), max(bounds[1], b), min(bounds[2], c), min(bounds[3], d)]
        return bounds if bounds[2] - bounds[0] > 4 and bounds[3] - bounds[1] > 10 else None

    def settle(self):
        previous, stable = None, 0
        deadline = time.monotonic() + 8
        while True:
            signature = [(n["type"], n["bounds"]) for n in self.tree()["nodes"]]
            stable = stable + 1 if signature == previous else 0
            if stable >= 2:
                return
            require(time.monotonic() < deadline, "UI geometry did not settle")
            previous = signature
            time.sleep(.08)

    def scroll(self, forward):
        # Both ends stay inside the fixture's scrolling body, below its fixed
        # toolbar/status rows. Starting over those rows does not scroll content.
        start, end = (.86, .59) if forward else (.59, .86)
        self.device.swipe(self.width - self.dp(28), round(self.height * start),
                          self.width - self.dp(28), round(self.height * end), 180, hold_ms=160)
        self.settle()

    @staticmethod
    def matches(node, text, prefix=False):
        # Composite titles and checkbox captions can be intrinsic text rather
        # than child LVGL labels; use the exported text instead of type=label.
        value = node.get("text", "")
        return value.startswith(text) if prefix else value == text

    def text(self, prefix, tree=None):
        found = [n.get("text", "") for n in (tree or self.tree())["nodes"] if self.matches(n, prefix, True)]
        require(len(found) == 1, "Fixture state missing or ambiguous: " + repr((prefix, found)))
        return found[0]

    def wait_text(self, prefix, expected):
        return wait_for(lambda: self.text(prefix), lambda value: value == expected,
                        "Asynchronous JS/UI result did not apply")

    def find(self, text, scroll=True, panel=False):
        for direction in ((False, True) if scroll else (False,)):
            previous, unchanged = None, 0
            for _ in range(12 if scroll else 1):
                tree = self.tree()
                found = [n for n in tree["nodes"] if self.matches(n, text) and self.clipped(n, panel)]
                if found:
                    # Prefer deepest exported text if a composite also exports
                    # its own caption. Both point to the same visible control.
                    return max(found, key=lambda n: n["depth"])
                signature = [(n["type"], n["bounds"]) for n in tree["nodes"]]
                unchanged = unchanged + 1 if signature == previous else 0
                if unchanged >= 2:
                    break
                previous = signature
                if scroll:
                    self.scroll(direction)
        raise RuntimeError("Visible fixture text not found: " + text)

    def tap(self, text, scroll=True, panel=False):
        node = self.find(text, scroll, panel)
        a, b, c, d = self.clipped(node, panel)
        self.device.tap((a + c) // 2, (b + d) // 2)

    def open(self, app_id=APP_ID, new_context=False):
        previous = self.resume_epoch
        already = self.device.state()["active"] == app_id
        self.device.request("OPEN", app_id, timeout=10)
        wait_for(self.device.state, lambda s: s["active"] == app_id, "App did not become foreground")
        if app_id == OTHER_ID:
            return self.wait_text("other=", "other=ready")
        expected = 0 if new_context else previous + (0 if already else 1)
        def ready(tree):
            values = [n.get("text", "") for n in tree["nodes"] if self.matches(n, "ready=", True)]
            return values and ";state=foreground" in values[0] and int(values[0].split(";", 1)[0][6:]) >= expected
        tree = wait_for(self.tree, ready, "Asynchronous app startup/resume did not finish")
        self.resume_epoch = int(self.text("ready=", tree).split(";", 1)[0][6:])
        return tree

    def back(self):
        self.device.swipe(self.dp(3), self.height // 2, self.dp(90), self.height // 2, 180)
        time.sleep(.2)

    def panel(self, notifications, cross_middle=False):
        self.device.request("DBG_HOME")
        x = self.width // 4 if notifications else self.width * 3 // 4
        end_x = self.width - x if cross_middle else x
        start_y, middle_y, end_y = self.dp(8), self.dp(115), self.dp(230)
        try:
            self.device.touch(x, start_y, True)
            # Establish a vertical pull before crossing horizontally. A
            # diagonal with dx>dy is correctly rejected as a top pull.
            first_y = self.dp(48)
            for step in range(1, 4):
                self.device.touch(x, round(start_y + (first_y - start_y) * step / 3), True)
                time.sleep(.025)
            for step in range(1, 7):
                self.device.touch(round(x + (end_x - x) * step / 6),
                                  round(first_y + (middle_y - first_y) * step / 6), True)
                time.sleep(.025)
            state = wait_for(self.device.state, lambda s: s["tracking"] and
                             bool(s.get("notifications")) == notifications and bool(s["quick"]) != notifications,
                             "Panel did not track from initial touch side")
            require(abs(state["quick_y"] - (-self.height + middle_y - start_y)) <= self.dp(3),
                    "Panel did not follow actual drag distance")
            self.device.touch(end_x, end_y, True)
        finally:
            self.device.touch(end_x, end_y, False)
        settled = wait_for(self.device.state, lambda s: not s["tracking"] and
                           bool(s.get("notifications")) == notifications and bool(s["quick"]) != notifications,
                           "Panel did not settle on initial side")
        time.sleep(.25)
        return {"following": state, "settled": settled}

    def close_panel(self):
        self.device.swipe(self.width // 2, self.dp(80), self.width // 2, self.dp(2), 180)
        return wait_for(self.device.state, lambda s: not s["quick"] and not s.get("notifications") and not s["tracking"],
                        "Panel did not close")


def run(device, args, report, save):
    hello = device.request("HELLO", timeout=8, attempts=3)
    require("storage=ready" in hello and "debug=1" in hello, "Storage/debug service unavailable")
    actual = device.request("DBG_BUILD")
    elf = Path(args.elf) if args.elf else ROOT / "build/ouse_core_desktop.elf"
    require(actual == hashlib.sha256(elf.read_bytes()).hexdigest(), "Board and supplied ELF do not match")
    initial_ids = installed_ids(device)
    require(not set(OWN_IDS) & set(initial_ids), "Fixture ID already installed; refusing overwrite")
    initial_notices = notices(device)
    require(not any(not item["ongoing"] for item in initial_notices["items"]),
            "Existing ordinary notifications would be affected by Clear all; preserve them and stop")
    report.update(hello=hello, elf_sha256=actual, initial_ids=initial_ids, initial_state=device.state(),
                  initial_runtime=stats(device), initial_notifications=initial_notices, owns_test_ids=True)
    data = build_fixtures(device.output / "packages")
    report["packages"] = {key: {"bytes": len(value), "sha256": hashlib.sha256(value).hexdigest()} for key, value in data.items()}
    save()
    ui = UI(device)

    def own(app_id=APP_ID):
        return [item for item in notices(device)["items"] if item["appId"] == app_id]

    def wait_own(predicate, app_id=APP_ID):
        return wait_for(lambda: own(app_id), predicate, "Notification storage did not apply")

    def record(stage, **details):
        require(healthy(device), "Board/UART failed")
        report["stages"].append(dict(stage=stage, time=time.time(), runtime=stats(device), **details))
        save()
        print("Real-board stage 2:", stage, "passed", flush=True)

    device.request("DBG_HOME")
    for app_id in OWN_IDS:
        upload(device, data[app_id], app_id)
    require(installed_ids(device) == sorted(initial_ids + list(OWN_IDS)), "Other packages changed during install")
    ui.open(new_context=True)
    ui.tap("List action")
    ui.wait_text("result=", "result=list-action")
    ui.tap("Second item")
    ui.wait_text("result=", "result=list-second")
    ui.tap("Style")
    ui.wait_text("result=", "result=style-merged")
    card_caption = ui.find("Merged style")
    card = next((node for node in reversed(card_caption["_parents"]) if node.get("border_width") == ui.dp(3)), None)
    require(card is not None and card.get("bg", "").lower() == "#244a68" and
            card.get("border", "").lower() == "#f2c04a" and card.get("radius") == ui.dp(20),
            "Partial setStyle overwrote prior card colors/radius: " + repr(card))
    device.screenshot(device.output / "widgets.jpg")
    record("composite_controls_and_style_merge")

    ui.tap("Disable")
    ui.wait_text("result=", "result=enabled:false;clicks:0")
    disabled_caption = ui.find("Target")
    disabled_button = next((node for node in reversed(disabled_caption["_parents"]) if node["type"] == "button"), None)
    require(disabled_button is not None and disabled_button.get("disabled") and
            disabled_button.get("bg", "").lower() == "#323238", "Disabled style did not reach button")
    ui.tap("Target")
    ui.wait_text("result=", "result=enabled:false;clicks:0")
    ui.tap("Disable")
    ui.wait_text("result=", "result=enabled:true;clicks:0")
    target_caption = ui.find("Target")
    a, b, c, d = ui.clipped(target_caption)
    x, y = (a + c) // 2, (b + d) // 2
    try:
        device.touch(x, y, True)
        time.sleep(.08)
        pressed_tree = ui.tree()
        target_nodes = [node for node in pressed_tree["nodes"] if node["type"] == "button" and
                        node["bounds"][0] <= x <= node["bounds"][2] and node["bounds"][1] <= y <= node["bounds"][3]]
        require(len(target_nodes) == 1 and target_nodes[0].get("bg", "").lower() == "#805020",
                "Pressed state style did not reach actual button: " + repr(target_nodes))
    finally:
        device.touch(x, y, False)
    ui.wait_text("result=", "result=target-clicks:1")
    for _ in range(12):
        nodes = [n for n in ui.tree()["nodes"] if n["type"] == "switch" and ui.clipped(n)]
        if nodes:
            a, b, c, d = ui.clipped(nodes[0])
            device.tap((a + c) // 2, (b + d) // 2)
            break
        ui.scroll(True)
    else:
        raise RuntimeError("State switch not visible")
    ui.wait_text("result=", "result=checked:true")
    switches = [node for node in ui.tree()["nodes"] if node["type"] == "switch"]
    require(len(switches) == 1 and switches[0].get("checked") and switches[0].get("bg", "").lower() == "#2a8d62",
            "Checked state style did not reach actual switch")
    record("disabled_and_checked_state_events")

    for title, modal in (("Dialog", "dialog"), ("Sheet", "sheet")):
        ui.tap(title)
        ui.wait_text("result=", f"result=show:{modal}:true")
        if modal == "dialog":
            device.screenshot(device.output / "dialog.jpg")
        ui.back()
        ui.wait_text("result=", f"result=dismiss:{modal}:back:false")
        require(device.state()["active"] == APP_ID, "Modal Back navigated away from app")
        ui.tap(title)
        ui.wait_text("result=", f"result=show:{modal}:true")
        device.tap(ui.dp(12), ui.dp(52))
        ui.wait_text("result=", f"result=dismiss:{modal}:backdrop:false")
        ui.tap(title)
        ui.tap("Dialog done" if modal == "dialog" else "Sheet done", scroll=False)
        ui.wait_text("result=", f"result=hide:{modal}:false")
    record("modal_back_backdrop_and_programmatic_dismiss")

    ui.tap("Publish")
    ui.wait_text("result=", "result=publish:true:true")
    wait_own(lambda items: sorted(item["id"] for item in items) == ["ongoing", "ordinary"])
    ui.tap("Update")
    ui.wait_text("result=", "result=update:true")
    updated = wait_own(lambda items: len(items) == 2 and any(x["id"] == "ordinary" and
                       x["title"] == "Stage2 updated" and x["payload"] == "stage2-updated" for x in items))
    record("notification_publish_replace", notifications=updated)

    ui.open(OTHER_ID)
    ui.tap("Other publish")
    ui.wait_text("other-result=", "other-result=publish:true")
    wait_own(lambda items: len(items) == 1 and items[0]["id"] == "ordinary", OTHER_ID)
    require(len(own()) == 2, "Other app replaced first app's same notification ID")
    ui.tap("Other inspect")
    ui.wait_text("other-result=", "other-result=own:ordinary")
    ui.tap("Other cancel")
    ui.wait_text("other-result=", "other-result=cancel:true")
    wait_own(lambda items: not items, OTHER_ID)
    require(len(own()) == 2, "Other app cancelled a foreign notification")
    record("notification_app_ownership")

    # Notification state survives JS context destruction. The queued intent
    # must be delivered after the newly created app installs its JS listener.
    device.request("DBG_HOME")
    device.request("DBG_CLOSE", APP_ID, timeout=10)
    wait_for(lambda: stats(device), lambda value: value["contexts"] == report["initial_runtime"]["contexts"] + 1,
             "Closed main app retained its context")
    left = ui.panel(True, cross_middle=True)
    require(left["settled"]["notification_cards"] >= 2, "Notification cards missing")
    device.screenshot(device.output / "notifications.jpg")
    ui.tap("Stage2 updated", scroll=False, panel=True)
    wait_for(device.state, lambda s: s["active"] == APP_ID and not s.get("notifications"), "Notification did not launch owning app")
    ui.resume_epoch = 0
    ui.open(new_context=True)
    ui.wait_text("intent=", "intent=ordinary:stage2-updated")
    right = ui.panel(False, cross_middle=True)
    ui.close_panel()
    record("initial_side_gesture_and_closed_app_open_intent", left=left, right=right)

    ui.open()
    ui.tap("Background")
    device.request("DBG_HOME")
    background = wait_own(lambda items: any(x["id"] == "background" and x["payload"] == "stage2-background" and
                                          x["body"] == "Published while background" for x in items))
    require(not device.state()["active"], "Background publication stole foreground")
    ui.open()
    ui.wait_text("result=", "result=background-published:true:background")
    record("background_notification", notifications=background)

    ui.panel(True)
    # Clear all is safe because the precondition preserved external messages;
    # still refuse if a concurrent external ordinary message appeared.
    require(not any(x["appId"] not in OWN_IDS and not x["ongoing"] for x in notices(device)["items"]),
            "Concurrent external message appeared; refuse Clear all")
    for text in ("全部清除", "清除全部", "Clear all"):
        try:
            ui.find(text, scroll=False, panel=True)
        except RuntimeError:
            continue
        ui.tap(text, scroll=False, panel=True)
        break
    else:
        raise RuntimeError("Notification Clear all control not found")
    retained = wait_own(lambda items: len(items) == 1 and items[0]["id"] == "ongoing" and items[0]["ongoing"])
    ui.close_panel()
    ui.open()
    ui.tap("Cancel")
    ui.wait_text("result=", "result=cancel:false:true")
    wait_own(lambda items: not items)
    record("clear_ordinary_keep_ongoing_and_cancel", retained=retained)

    ui.tap("Quota")
    ui.wait_text("result=", "result=quota:1111110;own:6")
    wait_own(lambda items: len(items) == 6)
    ui.tap("Reset notices")
    ui.wait_text("result=", "result=reset:0")
    wait_own(lambda items: not items)
    record("per_app_notification_quota")

    # Uninstall while the notification center owns visible card widgets. It
    # must remove records and cards without retaining application pointers.
    ui.tap("Publish")
    ui.wait_text("result=", "result=publish:true:true")
    ui.panel(True)
    before = device.state()
    device.request("REMOVE", APP_ID, timeout=10)
    wait_own(lambda items: not items)
    after = wait_for(device.state, lambda state: state["notification_cards"] <= before["notification_cards"] - 2,
                     "Uninstall retained visible notification cards")
    ui.close_panel()
    device.request("REMOVE", OTHER_ID, timeout=10)
    final_runtime = wait_for(lambda: stats(device), lambda value: value["contexts"] == report["initial_runtime"]["contexts"],
                             "Fixture contexts not released")
    require(installed_ids(device) == initial_ids, "Cleanup changed other packages")
    final_notices = notices(device)
    require(final_notices["items"] == initial_notices["items"], "Cleanup changed external notifications")
    heap = device.request("DBG_HEAP", timeout=10)
    require(device.request("DBG_CHECK", timeout=20, attempts=1) == "ok=1", "Heap integrity diagnostic failed")
    record("uninstall_visible_cards_cleanup", before=before, after=after, heap=heap)
    report.update(cleanup_completed=True, final_runtime=final_runtime, final_notifications=final_notices)
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
            (device.output / "phase2-result.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")

    try:
        device = Device(args.port, args.output, args.elf)
        save()
        run(device, args, report, save)
        report["status"] = "passed"
    except (Exception, KeyboardInterrupt) as error:
        report.update(status="interrupted" if isinstance(error, KeyboardInterrupt) else "failed",
                      error={"type": type(error).__name__, "message": str(error)})
        print("Stage 2 real board:", report["status"], error, file=sys.stderr, flush=True)
        if device is not None and healthy(device):
            try:
                report.update(failure_state=device.state(), failure_runtime=stats(device), failure_notifications=notices(device))
                (device.output / "failure-tree.json").write_text(json.dumps(device.tree(), ensure_ascii=False, indent=2), encoding="utf-8")
            except Exception as diagnostic_error:
                report["failure_diagnostic_error"] = str(diagnostic_error)
        exit_code = 130 if isinstance(error, KeyboardInterrupt) else 1
    finally:
        if device is not None:
            if report.get("owns_test_ids") and healthy(device):
                try:
                    for app_id in OWN_IDS:
                        if app_id in installed_ids(device):
                            device.request("REMOVE", app_id, timeout=10)
                    require(installed_ids(device) == report["initial_ids"], "Final cleanup changed other packages")
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
