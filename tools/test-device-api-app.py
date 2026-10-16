"""Exercise the installed API Test BPK through actual board pointer input.

Updates and keeps user.api-test. Never removes packages, clears user storage,
enters network credentials, resets, flashes, or builds firmware. Only the absent
default demo key may be created/deleted; an existing demo value is preserved.
The caller owns COM10; this file does nothing until main() is explicitly run.
"""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import re
import time

import bpk
from device import Device

ROOT = Path(__file__).resolve().parents[1]
APP_ID = "user.api-test"
SPEC = importlib.util.spec_from_file_location("api_app_helpers", ROOT / "tools/test-device-phase3.py")
HELPERS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(HELPERS)
require, wait_for, upload = HELPERS.require, HELPERS.wait_for, HELPERS.upload
stats, installed_ids, healthy = HELPERS.stats, HELPERS.installed_ids, HELPERS.healthy


def integers(device, verb):
    return {key: int(value) for key, value in
            (part.split("=", 1) for part in device.request(verb).split(","))}


def notices(device):
    return json.loads(device.resource("DBG_NOTICES").decode("utf-8"))["items"]


class UI:
    def __init__(self, device):
        self.device = device
        self.document = json.loads((ROOT / "apps/api-test/page.json").read_text(encoding="utf-8"))
        self.pages = {page["id"]: page["root"] for page in self.document["pages"]}
        state = device.state()
        self.width, self.height, self.dpi = state["width"], state["height"], state["dpi"]
        self.current = "home"
        self.fault_seen = False

    def dp(self, value):
        return round(value * self.dpi / 160)

    def labels(self, key):
        return {dictionary[key] for dictionary in self.document["translations"].values() if key in dictionary}

    def definition(self, node_id):
        def find(node):
            if node["id"] == node_id:
                return node
            for child in node.get("children", []):
                result = find(child)
                if result:
                    return result
            return None
        node = find(self.pages[self.current])
        require(node is not None, "Missing source control: " + node_id)
        return node

    def tree(self):
        tree = self.device.tree()
        (self.device.output / "api-last-tree.json").write_text(
            json.dumps(tree, ensure_ascii=False, indent=2), encoding="utf-8")
        if any(node.get("text") in ("Application error", "应用运行出错") for node in tree["nodes"]):
            self.fault_seen = True
            raise RuntimeError("API application fault page is visible; stop functional inputs")
        stack = []
        for node in tree["nodes"]:
            while stack and stack[-1]["depth"] >= node["depth"]:
                stack.pop()
            node["_parents"] = tuple(stack)
            stack.append(node)
        return tree

    def clip(self, node):
        a, b, c, d = self.dp(10), self.dp(32), self.width - self.dp(10), self.height - self.dp(24)
        for parent in (*node["_parents"], node):
            x, y, z, w = parent["bounds"]
            a, b, c, d = max(a, x), max(b, y), min(c, z), min(d, w)
        return (a, b, c, d) if c - a > 4 and d - b > self.dp(12) else None

    def scroll(self, forward):
        x = self.width - self.dp(14)
        self.device.swipe(x, round(self.height * (.66 if forward else .34)),
                          x, round(self.height * (.34 if forward else .68)), 180, hold_ms=140)
        previous = None
        for _ in range(4):
            signature = [(node["type"], node["bounds"]) for node in self.tree()["nodes"]]
            if signature == previous:
                return
            previous = signature
            time.sleep(.08)

    def find(self, predicate, visible=True):
        for forward in (False, True):
            previous = None
            for _ in range(6):
                tree = self.tree()
                found = [node for node in tree["nodes"] if predicate(node)]
                if not visible and found:
                    return found[-1]
                shown = [node for node in found if self.clip(node)]
                if shown:
                    require(len(shown) == 1, "Ambiguous visible API control")
                    return shown[0]
                signature = [(node["type"], node["bounds"]) for node in tree["nodes"]]
                if signature == previous:
                    break
                previous = signature
                self.scroll(forward)
        raise RuntimeError("API control could not be brought into view")

    def tap_node(self, node):
        bounds = self.clip(node)
        require(bounds is not None, "Control is outside viewport")
        a, b, c, d = bounds
        return self.device.tap((a + c) // 2, (b + d) // 2)

    def tap_id(self, node_id):
        definition = self.definition(node_id)
        key = definition.get("textKey")
        if not key and not definition.get("text") and node_id in self.document["translations"]["en"]:
            key = node_id  # checkbox caption is localized by application JS
        labels = self.labels(key) if key else {definition.get("text", "")}
        node = self.find(lambda item: item["type"] in ("label", "checkbox") and item["text"] in labels)
        return self.tap_node(node)

    def text(self, predicate):
        values = [node["text"] for node in self.tree()["nodes"]
                  if node["type"] == "label" and predicate(node["text"])]
        require(len(values) <= 1, "Ambiguous API telemetry: " + repr(values))
        return values[0] if values else None

    def wait_text(self, predicate, message, timeout=12):
        return wait_for(lambda: self.text(predicate), bool, message, timeout)

    def page(self, page):
        names = self.labels(page)
        def ready():
            return any(node["type"] == "label" and node["text"] in names and
                       not any(parent["type"] == "button" for parent in node["_parents"])
                       for node in self.tree()["nodes"])
        wait_for(ready, bool, "Page did not enter: " + page, 12)
        self.current = page

    def detect_page(self, allow_missing=False):
        tree = self.tree()
        for page in self.pages:
            if any(node["type"] == "label" and node["text"] in self.labels(page) and
                   not any(parent["type"] == "button" for parent in node["_parents"])
                   for node in tree["nodes"]):
                self.current = page
                return page
        if allow_missing:
            return None
        raise RuntimeError("Current API page has no ready title")

    def edge(self, left=True):
        self.device.swipe(2 if left else self.width - 3, self.dp(150),
                          self.dp(96) if left else self.width - self.dp(96), self.dp(150), 180)

    def goto(self, page):
        if self.device.state()["active"] != APP_ID:
            self.device.request("DBG_OPEN", APP_ID, timeout=10)
            time.sleep(.15)
        wait_for(lambda: self.detect_page(allow_missing=True), bool, "API page did not become ready", 12)
        for _ in range(8):
            if self.current == "home":
                break
            old = self.current
            self.tap_id("back")
            parent = "controls" if old in ("widgets", "styles", "inputResize", "inputPan", "inputOverlay") else "resources" if old == "storage" else "home"
            self.page(parent)
        require(self.current == "home", "Could not return to API home")
        path = (["controls", page] if page in ("widgets", "styles", "inputResize", "inputPan", "inputOverlay") else
                ["resources", "storage"] if page == "storage" else [] if page == "home" else [page])
        for target in path:
            self.tap_id("go_" + target)
            self.page(target)

    def control(self, kind, index=0):
        def predicate(node):
            if node["type"] != kind:
                return False
            peers = [item for item in self.last_nodes if item["type"] == kind]
            return len(peers) > index and node is peers[index]
        original = self.tree
        def snapshot():
            tree = original()
            self.last_nodes = tree["nodes"]
            return tree
        self.tree = snapshot
        try:
            return self.find(predicate)
        finally:
            self.tree = original

    def slider(self, target, reader, minimum=0, maximum=100):
        adjustment = 0
        for attempt in range(4):
            actual = reader()
            if actual == target:
                return
            node = self.control("slider")
            a, b, c, d = self.clip(node)
            x1, _, x2, _ = node["bounds"]
            end = round(x1 + (x2 - x1) * (target - minimum) / (maximum - minimum))
            if attempt:
                adjustment += round((target - actual) * (x2 - x1) / (maximum - minimum))
            end += adjustment
            end = min(x2, max(x1, end))
            self.device.swipe((a + c) // 2, (b + d) // 2, end, (b + d) // 2, 180)
            time.sleep(.4)
        require(reader() == target, "Slider target was not applied: " + str(target))

    def json_output(self):
        values = []
        for node in self.tree()["nodes"]:
            value = node["text"]
            if node["type"] != "label" or not (value.startswith(("{", "[")) or value in ("true", "false", "null")):
                continue
            try:
                values.append((True, json.loads(value)))
            except ValueError:
                pass
        require(len(values) <= 1, "Ambiguous private-data output")
        return values[0] if values else (False, None)

    def storage(self, action, predicate):
        self.tap_id(action)
        # The JSON output follows the editors/buttons inside the scrolling body.
        # DBG_TREE excludes clipped objects, so expose it before polling.
        for _ in range(4):
            if self.json_output()[0]:
                break
            self.scroll(True)
        return wait_for(self.json_output, lambda value: value[0] and predicate(value[1]),
                        "Storage result did not complete: " + action, 15)[1]

    def audio(self):
        value = self.wait_text(lambda text: text.startswith("Device ") and "Muted " in text, "Audio snapshot missing")
        mode = re.search(r"^Device (Yes|No) · (\w+)", value)
        level = re.search(r"Volume (\d+)% · Muted (Yes|No)", value)
        elapsed = re.search(r"\n(\d+) / (\d+) ms", value)
        request = re.search(r"Error (\d+) · (\d+)/(\d+)", value)
        require(all((mode, level, elapsed, request)), "Incomplete audio snapshot: " + value)
        return dict(available=mode[1] == "Yes", state=mode[2], volume=int(level[1]), muted=level[2] == "Yes",
                    position=int(elapsed[1]), recorded=int(elapsed[2]), error=int(request[1]),
                    completed=int(request[2]), request=int(request[3]))

    def audio_action(self, action):
        previous = self.audio()["request"]
        self.tap_id(action)
        def complete(state):
            require(state["error"] == 0 and state["state"] != "error", "Audio driver error: " + repr(state))
            return state["request"] > previous and state["completed"] == state["request"] and state["state"] == "idle"
        return wait_for(self.audio, complete, "Audio request did not actually finish: " + action, 15)

    def brightness(self):
        text = self.wait_text(lambda value: " dpi · " in value and " px / " in value, "Display snapshot missing")
        match = re.search(r"dpi · (\d+)% · (\d+)%", text)
        require(match is not None, "Brightness snapshot missing")
        return int(match[2])


def run(device, args, report, save):
    require(device.request("DBG_BUILD") == hashlib.sha256(Path(args.elf).read_bytes()).hexdigest(), "ELF mismatch")
    report.update(initial_ids=installed_ids(device), initial_state=device.state(), initial_runtime=stats(device),
                  initial_other_notices=[item for item in notices(device) if item["appId"] != APP_ID])
    require(report["initial_state"]["font"] in (100, 115, 130), "Initial text size cannot be restored through this application's controls")
    package = device.output / "api-test.bpk"
    require(bpk.pack(ROOT / "apps/api-test", package)["id"] == APP_ID, "Unexpected application ID")
    upload(device, package.read_bytes(), APP_ID)
    report["installed"] = True
    ui = UI(device)
    report["ui"] = ui

    def record(name, **details):
        require(healthy(device), "Device or transport failed")
        require(stats(device)["errors"] == report["initial_runtime"]["errors"], "Application generated a JS fault")
        report["stages"].append(dict(name=name, time=time.time(), **details))
        save()
        print("API App:", name, "passed", flush=True)

    device.request("DBG_OPEN", APP_ID)
    ui.page("home")
    ui.goto("display")
    if device.state()["locale"] != 1:
        ui.tap_id("language")
        wait_for(device.state, lambda state: state["locale"] == 1, "English locale did not apply")
    report["initial_brightness"] = ui.brightness()
    ui.goto("audio")
    report["initial_audio"] = ui.audio()
    require(report["initial_audio"]["available"] and report["initial_audio"]["state"] == "idle", "Audio is unavailable or already owned by other work")
    ui.goto("power")
    report["initial_timeout"] = ui.control("dropdown")["text"]
    report["preferences_captured"] = True
    save()

    for index, page in enumerate(("display", "audio", "wifi", "power", "controls", "messages", "resources")):
        ui.goto(page)
        if index % 3 == 0:
            ui.tap_id("back")
        else:
            ui.edge(index % 3 == 1)
        ui.page("home")
    record("all_seven_categories_header_and_both_edge_back")

    ui.goto("display")
    for field, action in (("theme", "theme"), ("font", "font"), ("locale", "language")):
        before = device.state()[field]
        ui.tap_id(action)
        after = wait_for(device.state, lambda state: state[field] != before, "Display preference did not change: " + field)
        for _ in range(3):
            if device.state()[field] == before:
                break
            ui.tap_id(action)
            time.sleep(.15)
        wait_for(device.state, lambda state: state[field] == before, "Display preference did not restore: " + field)
        record("display_" + field, changed=after[field], restored=before)
    device.screenshot(device.output / "api-display.jpg")

    ui.goto("audio")
    report["audio_test_owned"] = True
    save()
    ui.slider(8, lambda: ui.audio()["volume"])
    if ui.audio()["muted"]:
        ui.tap_id("mute")
        wait_for(ui.audio, lambda state: not state["muted"], "Unmute did not apply")
    results = {action: ui.audio_action(action) for action in ("tone", "record", "replay", "stop")}
    require(results["record"]["recorded"] > 0, "Recording did not produce PCM samples")
    record("audio_low_volume_real_request_completion", results=results)
    device.screenshot(device.output / "api-audio.jpg")

    ui.goto("wifi")
    before = ui.wait_text(lambda value: " dBm · Error " in value, "Wi-Fi snapshot missing")
    if before.startswith(("Off ", "Connecting ", "Initializing ")):
        record("wifi_snapshot_without_changing_disabled_or_busy_network", snapshot=before, scan_executed=False)
    else:
        old_request = int(re.search(r"(\d+)/(\d+)$", before)[2])
        ui.tap_id("scan")
        def scan_done():
            value = ui.text(lambda text: " dBm · Error " in text)
            if not value:
                return None
            match = re.search(r"Error (\d+) / (\d+)\n(\d+)/(\d+)$", value)
            require(match is not None, "Incomplete Wi-Fi request status: " + value)
            require(int(match[1]) == 0, "Wi-Fi scan driver error: " + value)
            return value if int(match[4]) > old_request and match[3] == match[4] and not value.startswith("Scanning ") else None
        result = wait_for(scan_done, bool, "Wi-Fi scan did not actually finish", 35)
        record("wifi_scan_read_only_credentials", snapshot=result, scan_executed=True)

    ui.goto("power")
    system = ui.wait_text(lambda value: "Flash " in value and "CPU " in value and "RAM " in value, "System telemetry missing")
    ui.tap_id("sleep")
    ui.wait_text(lambda value: "Asleep Yes" in value, "Screen-off request did not complete")
    device.request("DBG_OPEN", APP_ID)
    ui.wait_text(lambda value: "Asleep No" in value, "Display did not wake")
    require(ui.control("dropdown")["text"] == report["initial_timeout"], "Power timeout changed unexpectedly")
    record("power_system_snapshot_sleep_and_wake", snapshot=system)

    ui.goto("widgets")
    ui.tap_id("target")
    ui.wait_text(lambda value: value.startswith("Click event · 1"), "Button event not delivered")
    switch = ui.control("switch")
    ui.tap_node(switch)
    ui.wait_text(lambda value: value == "Switch event · true", "Switch event not delivered")
    require(ui.control("switch")["checked"], "Switch native visual state is not checked")
    ui.tap_id("checkbox")
    ui.wait_text(lambda value: value == "Checkbox event · true", "Checkbox event not delivered")
    device.screenshot(device.output / "api-widgets.jpg")
    record("controls_native_button_switch_checkbox")
    ui.goto("styles")
    ui.tap_id("stylePatch")
    ui.wait_text(lambda value: value == "Style patched", "Style patch did not apply")
    ui.tap_id("openDialog")
    ui.wait_text(lambda value: value.startswith("Modal shown · dialog"), "Dialog did not open")
    ui.edge()
    ui.wait_text(lambda value: value == "Modal dismissed · dialog / back", "Back did not dismiss modal first")
    ui.page("styles")
    ui.tap_id("styleReset")
    record("controls_style_patch_modal_back_and_reset")

    for page, mode, input_index, keyboard_type in (("inputResize", 0, 0, 0), ("inputPan", 1, 1, 1), ("inputOverlay", 2, 2, 2)):
        ui.goto(page)
        ui.tap_node(ui.control("input", input_index))
        ime = wait_for(lambda: integers(device, "DBG_IME"), lambda value: value["visible"] == 1, "Keyboard did not open")
        require(ime["mode"] == mode and ime["keyboard_type"] == keyboard_type, "Wrong input avoidance/type: " + repr(ime))
        if mode != 2:
            require(ime["input_y2"] < ime["keyboard_y1"], "Keyboard covers focused input")
        x = round(ui.width * (.10 if keyboard_type == 1 else .14))
        y = ime["keyboard_y1"] + (ime["keyboard_y2"] - ime["keyboard_y1"]) // 8
        device.tap(x, y)
        field = ("text", "number", "password")[input_index]
        ui.wait_text(lambda value: value == "Input changed · " + field + " / 1", "Physical keyboard key did not reach app")
        require(all(not node["text"] for node in ui.tree()["nodes"] if node["type"] == "input"), "Input content leaked through debug tree")
        if mode in (0, 2):
            device.screenshot(device.output / ("api-" + page + ".jpg"))
        ui.edge()
        require(integers(device, "DBG_IME")["visible"] == 0, "Back did not hide keyboard")
        ui.page(page)
        record("keyboard_" + page, keyboard=ime)

    ui.goto("resources")
    ui.tap_id("readText")
    ui.wait_text(lambda value: value.startswith("Text read · "), "Text asset not read")
    ui.tap_id("readBytes")
    ui.wait_text(lambda value: value == "Bytes read · 0, 17, 128, 255", "Binary asset not read")
    device.screenshot(device.output / "api-assets-blue.jpg")
    ui.tap_id("swap")
    ui.wait_text(lambda value: value == "Image changed · green", "Image resource did not switch")
    device.screenshot(device.output / "api-assets-green.jpg")
    record("assets_text_binary_and_image_switch")
    ui.goto("storage")
    keys = ui.storage("keys", lambda value: isinstance(value, list))
    report["initial_private_keys"] = keys
    if "demo" not in keys:
        report["owns_demo_key"] = True
        save()
        value = dict(message="hello", count=1)
        ui.storage("set", lambda output: output == value)
        ui.storage("keys", lambda output: output == sorted(keys + ["demo"]))
        ui.storage("get", lambda output: output == value)
        ui.storage("remove", lambda output: output is True)
        ui.storage("get", lambda output: output is None)
        require(ui.storage("keys", lambda output: output == keys) == keys, "Other private keys changed")
        report["owns_demo_key"] = False
        record("private_data_write_read_remove_preserves_other_keys", original_key_count=len(keys))
    else:
        ui.storage("info", lambda output: isinstance(output, dict) and output.get("keyLimit") == 32)
        record("private_data_existing_demo_preserved", writes_executed=False, reason="Existing demo key belongs to user")
    device.screenshot(device.output / "api-storage.jpg")

    ui.goto("messages")
    ui.tap_id("publish")
    wait_for(lambda: notices(device), lambda items: any(item["appId"] == APP_ID and item["id"] == "api-demo" for item in items), "Notification not published")
    ui.tap_id("update")
    wait_for(lambda: notices(device), lambda items: any(item["appId"] == APP_ID and item["payload"] == "messages:updated" for item in items), "Notification not updated")
    device.request("DBG_CLOSE", APP_ID)
    device.request("DBG_HOME")
    device.swipe(ui.dp(40), 2, ui.dp(40), ui.dp(350), 240)
    wait_for(device.state, lambda state: state["notifications"], "Notification shade not opened")
    ui.tap_node(ui.find(lambda node: node["type"] == "label" and node["text"] == "Updated notice"))
    ui.page("messages")
    ui.wait_text(lambda value: "api-demo · messages:updated" in value and "Notice opens 1" in value, "Notification did not reopen app and deliver payload")
    ui.tap_id("ongoing")
    wait_for(lambda: notices(device), lambda items: any(item["appId"] == APP_ID and item["id"] == "api-ongoing" and item["ongoing"] for item in items), "Ongoing notification not published")
    ui.tap_id("cancel")
    wait_for(lambda: notices(device), lambda items: not any(item["appId"] == APP_ID and item["id"] in ("api-demo", "api-ongoing") for item in items), "Test notices were not canceled")
    record("notifications_publish_update_close_click_ongoing_cancel")

    def ticks():
        value = ui.wait_text(lambda text: "Background ticks " in text, "Background state missing")
        return int(re.search(r"Background ticks (\d+)", value)[1])
    ui.tap_id("backgroundOn")
    ui.wait_text(lambda value: "Background Yes" in value, "Background grant not active")
    before = ticks()
    ui.tap_id("backgroundHome")
    wait_for(device.state, lambda state: state["active"] == "", "Home did not background test app")
    time.sleep(1.3)
    device.request("DBG_OPEN", APP_ID)
    ui.page("messages")
    after = ticks()
    require(after > before, "Background timer did not continue")
    ui.tap_id("backgroundOff")
    ui.wait_text(lambda value: "Background No" in value, "Background lease not stopped")
    frozen = ticks()
    ui.tap_id("backgroundHome")
    time.sleep(1.3)
    device.request("DBG_OPEN", APP_ID)
    ui.page("messages")
    require(ticks() == frozen, "Default suspended app continued background business code")
    record("background_grant_runs_and_end_freezes", before=before, after=after, frozen=frozen)
    device.screenshot(device.output / "api-messages.jpg")
    require(device.request("DBG_CHECK", timeout=20, attempts=1) == "ok=1", "Heap integrity failed")
    record("heap_integrity_and_no_new_runtime_errors", heap=device.request("DBG_HEAP"))


def cleanup(device, report):
    ui = report.get("ui")
    if not ui or not report.get("installed") or not healthy(device):
        return
    errors = []
    def restore(name, operation):
        if not healthy(device):
            errors.append(dict(step=name, error="Device stopped; no additional input sent"))
            return
        try:
            operation()
        except Exception as error:
            errors.append(dict(step=name, error=str(error)))
    def stop_tests():
        if ui.fault_seen:
            # Recreate only this script's app so its supported settings controls
            # can restore preferences. A firmware panic still forbids inputs.
            device.request("DBG_CLOSE", APP_ID)
            device.request("DBG_OPEN", APP_ID)
            ui.page("home")
            ui.fault_seen = False
        device.request("DBG_OPEN", APP_ID)
        ui.goto("messages")
        ui.tap_id("cleanup")
        wait_for(lambda: notices(device), lambda items: not any(item["appId"] == APP_ID and item["id"] in ("api-demo", "api-ongoing") for item in items), "Test notice cleanup incomplete")
    restore("notifications_background_and_audio", stop_tests)
    def use_english_for_telemetry():
        ui.goto("display")
        if device.state()["locale"] != 1:
            ui.tap_id("language")
            wait_for(device.state, lambda value: value["locale"] == 1, "Cleanup telemetry language did not apply")
    restore("temporary_telemetry_locale", use_english_for_telemetry)
    if report.get("owns_demo_key"):
        def remove_owned_key():
            # Fresh context guarantees keyDraft='demo'; no user input is edited.
            device.request("DBG_CLOSE", APP_ID)
            device.request("DBG_OPEN", APP_ID)
            ui.page("home")
            ui.goto("storage")
            ui.storage("remove", lambda value: isinstance(value, bool))
            ui.storage("get", lambda value: value is None)
            ui.storage("keys", lambda value: value == report["initial_private_keys"])
            report["owns_demo_key"] = False
        restore("only_owned_demo_key", remove_owned_key)
    if report.get("audio_test_owned") and "initial_audio" in report:
        def restore_audio():
            ui.goto("audio")
            ui.audio_action("stop")
            ui.slider(report["initial_audio"]["volume"], lambda: ui.audio()["volume"])
            if ui.audio()["muted"] != report["initial_audio"]["muted"]:
                ui.tap_id("mute")
            wait_for(ui.audio, lambda value: value["volume"] == report["initial_audio"]["volume"] and value["muted"] == report["initial_audio"]["muted"] and value["state"] == "idle", "Audio preferences did not restore")
        restore("audio_preferences", restore_audio)
    def restore_display():
        ui.goto("display")
        original = report["initial_state"]
        for field, action in (("theme", "theme"), ("font", "font")):
            for _ in range(3):
                if device.state()[field] == original[field]:
                    break
                ui.tap_id(action)
                time.sleep(.15)
            require(device.state()[field] == original[field], "Preference did not restore: " + field)
        if "initial_brightness" in report:
            if ui.brightness() != report["initial_brightness"]:
                ui.slider(report["initial_brightness"], ui.brightness, 10, 100)
        if device.state()["locale"] != original["locale"]:
            ui.tap_id("language")
        wait_for(device.state, lambda value: all(value[field] == original[field] for field in ("theme", "font", "locale")), "Display preferences did not restore")
    restore("display_and_locale_preferences", restore_display)
    def verify_timeout():
        ui.goto("power")
        require(ui.control("dropdown")["text"] == report["initial_timeout"], "Screen timeout preference changed")
    if report.get("initial_timeout") is not None:
        restore("unchanged_screen_timeout", verify_timeout)
    def close_and_preserve():
        device.request("DBG_CLOSE", APP_ID)
        expected = sorted(set(report["initial_ids"]) | {APP_ID})
        require(installed_ids(device) == expected, "Installed package set changed unexpectedly")
        require(APP_ID not in device.state()["alive"], "API test instance remained live")
        actual_other = [item for item in notices(device) if item["appId"] != APP_ID]
        require(actual_other == report["initial_other_notices"], "Other applications' notifications changed")
        original = report["initial_state"]["active"]
        if original and original != APP_ID:
            device.request("DBG_OPEN", original)
        else:
            device.request("DBG_HOME")
        report.update(final_ids=installed_ids(device), final_state=device.state(), final_runtime=stats(device), app_kept_installed=True)
    restore("close_instance_keep_bpk_and_other_apps", close_and_preserve)
    report["cleanup_errors"] = errors
    report["cleanup_completed"] = not errors
    require(not errors, "Cleanup did not complete: " + repr(errors))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM10")
    parser.add_argument("--elf", default=str(ROOT / "build/ouse_core_desktop.elf"))
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    report = dict(started=time.time(), status="running", stages=[])
    device, exit_code = None, 0
    def save():
        if device:
            public = {key: value for key, value in report.items() if key != "ui"}
            (device.output / "api-app-result.json").write_text(json.dumps(public, ensure_ascii=False, indent=2), encoding="utf-8")
    try:
        device = Device(args.port, args.output, args.elf)
        device.request("HELLO")
        run(device, args, report, save)
        report["status"] = "passed"
    except (Exception, KeyboardInterrupt) as error:
        report.update(status="failed", error=str(error))
        exit_code = 1
        print("API App failed:", error, flush=True)
    finally:
        if device:
            try:
                cleanup(device, report)
            except Exception as error:
                report.update(status="failed", cleanup_error=str(error))
                exit_code = 1
            try:
                device.close()
            except Exception as error:
                report.update(status="failed", close_error=str(error))
                exit_code = 1
            report.update(finished=time.time(), panic_seen=bool(device.capture.panic),
                          unexpected_reboot=device.rebooted, serial_closed=not device.port.is_open)
            if report["panic_seen"] or report["unexpected_reboot"] or device.error is not None:
                report["status"], exit_code = "failed", 1
            save()
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
