"""Exercise real system UI, preferences and app lifetimes through one UART owner.

No network credentials or packages are changed. Preferences are read from the
actual device and restored after appearance/brightness experiments.
"""
import argparse
import json
import re
import sys
import time

from device import Device


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


class UI:
    """Reusable tree-based UI driver; every input goes through the real indev."""

    def __init__(self, device, report=None, save=None):
        self.device = device
        self.report = report if report is not None else {"events": []}
        self.report.setdefault("events", [])
        self.save = save or (lambda: None)
        self.last_tree = None
        self.geometry = self.state()
        self.width, self.height = self.geometry["width"], self.geometry["height"]
        self.density = self.geometry["dpi"]
        require(self.width >= 240 and self.height >= 320, "Unsupported display geometry")

    def alive(self):
        return not self.device.capture.panic and not self.device.rebooted and self.device.error is None

    def dp(self, value):
        return round(value * self.density / 160)

    def record(self, event, **data):
        self.report["events"].append({"time": time.time(), "event": event, **data})
        self.save()

    def state(self):
        return self.device.state()

    def tree(self):
        self.last_tree = self.device.tree()
        stack, result = [], []
        for node in self.last_tree["nodes"]:
            while stack and stack[-1]["depth"] >= node["depth"]:
                stack.pop()
            result.append({**node, "_parents": tuple(stack)})
            stack.append(node)
        return result

    def clip(self, node):
        # The tree can include partially clipped labels; never tap outside the
        # app content or the ancestor that actually owns the target.
        bounds = [self.dp(25), self.dp(42), self.width - self.dp(25), self.height - self.dp(37)]
        for item in (*node.get("_parents", ()), node):
            a, b, c, d = item["bounds"]
            bounds = [max(bounds[0], a), max(bounds[1], b), min(bounds[2], c), min(bounds[3], d)]
        return bounds if bounds[2] - bounds[0] >= 3 and bounds[3] - bounds[1] >= 3 else None

    def scroll(self, forward=True, nodes=None):
        nodes = nodes if nodes is not None else self.tree()
        x = self.width // 2
        # Start in the upper half, avoiding sliders and their extended hit area.
        # Vertical drags over a button/label scroll its parent rather than click.
        candidates = [self.dp(v) for v in (180, 150, 210, 120, 235, 95)]
        y = None
        for candidate in candidates:
            if not self.dp(60) <= candidate <= min(self.height // 2, self.height - self.dp(90)):
                continue
            sliders = [n for n in nodes if n["type"] == "slider"]
            if any(n["bounds"][0] - self.dp(10) <= x <= n["bounds"][2] + self.dp(10) and
                   n["bounds"][1] - self.dp(10) <= candidate <= n["bounds"][3] + self.dp(10)
                   for n in sliders):
                continue
            y = candidate
            break
        require(y is not None, "No safe scroll origin outside visible sliders")
        end = max(self.dp(55), y - self.dp(130)) if forward else min(self.height - self.dp(65), y + self.dp(180))
        require(abs(end - y) >= self.dp(32), "Insufficient room for safe scrolling")
        state = self.device.swipe(x, y, x, end, 220)
        # The UI keeps decelerating after release. A tree captured during that
        # motion is already stale by the time its UART download completes.
        previous = None
        deadline = time.monotonic() + 5
        while True:
            signature = [(n["type"], n["bounds"]) for n in self.tree()]
            if signature == previous:
                return state
            require(time.monotonic() < deadline, "Scroll geometry did not settle")
            previous = signature
            time.sleep(.08)

    def find_label(self, texts, scroll=True, backward=False):
        match = texts if callable(texts) else lambda value: value in ((texts,) if isinstance(texts, str) else texts)
        for direction in ((not backward, backward) if scroll else (True,)):
            unchanged, previous = 0, None
            for _ in range(28 if scroll else 1):
                nodes = self.tree()
                found = [n for n in nodes if n["type"] == "label" and match(n["text"]) and self.clip(n)]
                if found:
                    return found[0]
                if not scroll:
                    break
                # Changing clocks/telemetry must not hide that scrolling hit
                # its boundary. Geometry, rather than text, detects no motion.
                signature = [(n["type"], n["bounds"]) for n in nodes]
                unchanged = unchanged + 1 if signature == previous else 0
                if unchanged >= 2:
                    break
                previous = signature
                self.scroll(direction, nodes)
        raise RuntimeError("UI label not found in the scrollable viewport: " + repr(texts))

    def tap_label(self, texts, backward=False):
        node = self.find_label(texts, backward=backward)
        bounds = self.clip(node)
        self.record("tap_label", text=node["text"], bounds=node["bounds"])
        return self.device.tap((bounds[0] + bounds[2]) // 2, (bounds[1] + bounds[3]) // 2)

    def find_slider(self, index=0, scroll=True):
        for _ in range(24 if scroll else 1):
            nodes = self.tree()
            sliders = sorted((n for n in nodes if n["type"] == "slider" and self.clip(n)),
                             key=lambda n: n["bounds"][1])
            if len(sliders) > index:
                return sliders[index]
            self.scroll(True, nodes)
        raise RuntimeError("No visible slider at index " + str(index))

    def drag_slider(self, node, value, min_value, max_value):
        require(min_value <= value <= max_value and min_value < max_value, "Invalid slider target")
        x1, y1, x2, y2 = node["bounds"]
        clipped = self.clip(node)
        require(clipped is not None, "Slider is outside its visible viewport")
        y = (clipped[1] + clipped[3]) // 2
        start = max(x1, min(x2, (x1 + x2) // 2))
        end = round(x1 + (x2 - x1) * (value - min_value) / (max_value - min_value))
        self.device.touch(start, y, True)
        try:
            for step in range(1, 5):
                self.device.touch(round(start + (end - start) * step / 4), y, True)
                time.sleep(.025)
        finally:
            if self.alive():
                self.device.touch(end, y, False)
        time.sleep(.4)  # allow the existing 300 ms save debounce to finish
        self.record("drag_slider", target=value, range=[min_value, max_value], bounds=node["bounds"])
        return self.state()

    def wait_state(self, predicate, description="expected state", timeout=4):
        deadline = time.monotonic() + timeout
        while True:
            state = self.state()
            if predicate(state):
                return state
            if time.monotonic() >= deadline:
                raise RuntimeError(description + ": " + repr(state))
            time.sleep(.08)

    def open_app(self, app_id):
        self.device.request("DBG_OPEN", app_id, timeout=10, attempts=1)
        return self.wait_state(lambda s: s["active"] == app_id and not s["quick"] and not s["recents"], "Application did not open")

    def home(self):
        state = self.state()
        if state["quick"]:
            self.device.swipe(self.width // 2, self.dp(80), self.width // 2, self.dp(4), 180)
            state = self.state()
        if state["recents"]:
            # Blank area left of Clear all, below the cards; preserve instances.
            self.device.tap(self.dp(36), self.height - self.dp(70))
        elif state["active"]:
            self.device.swipe(self.width // 2, self.height - self.dp(6), self.width // 2,
                              self.height - self.dp(100), 180)
        return self.wait_state(lambda s: not s["active"] and not s["quick"] and not s["recents"] and not s["tracking"], "Home gesture failed")

    def overview(self):
        state = self.device.swipe(self.width // 2, self.height - self.dp(6), self.width // 2,
                                  self.height - self.dp(100), 180, 280)
        require(state["recents"] and not state["tracking"], "Held swipe did not open overview: " + repr(state))
        return state

    def checkpoint(self, name):
        heap = dict(field.split("=", 1) for field in self.device.request("DBG_HEAP", timeout=15).split(","))
        heap = {key: int(value) for key, value in heap.items()}
        integrity = self.device.request("DBG_CHECK", timeout=20, attempts=1)
        require(integrity == "ok=1", "Heap integrity check did not succeed: " + integrity)
        state = self.state()
        self.record("checkpoint", name=name, heap=heap, state=state)
        return {"heap": heap, "state": state}

    def read_brightness(self):
        self.open_app("ouse.api-test")
        node = self.find_label(lambda value: "Font:" in value and "Light:" in value, backward=True)
        match = re.search(r"Light:\s*(\d+)%", node["text"])
        require(match is not None, "API test did not expose current brightness")
        return int(match.group(1))

    def set_font(self, target):
        for _ in range(4):
            current = self.state()["font"]
            if current == target:
                return
            require(current in (100, 115, 130), "Unknown font preference")
            self.tap_label(("切换字号", "Change text size"))
            expected = {100: 115, 115: 130, 130: 100}[current]
            self.wait_state(lambda s: s["font"] == expected, "Text-size button did not apply")
        raise RuntimeError("Could not restore font preference")

    def set_locale(self, target):
        if self.state()["locale"] != target:
            self.tap_label(("切换语言", "Switch language"), backward=True)
            self.wait_state(lambda s: s["locale"] == target, "Language button did not apply")

    def set_theme(self, target):
        if self.state()["theme"] != target:
            self.tap_label(("切换明暗", "Toggle theme"), backward=True)
            self.wait_state(lambda s: s["theme"] == target, "Theme button did not apply")

    def api_brightness(self, target):
        self.open_app("ouse.api-test")
        self.find_label(("屏幕亮度", "Brightness"))
        self.drag_slider(self.find_slider(), target, 10, 100)
        actual = self.read_brightness()
        require(actual == target, f"Brightness target {target}, actual {actual}")

    def restore_preferences(self, original, appearance=True, brightness=True):
        if not self.alive():
            self.record("restore_skipped", reason="panic, reboot or serial failure")
            return
        if appearance:
            state = self.state()
            if any(state[key] != original[key] for key in ("theme", "locale", "font")):
                self.open_app("ouse.api-test")
                self.set_font(original["font"])
                self.set_locale(original["locale"])
                self.set_theme(original["theme"])
        if brightness:
            if self.read_brightness() != original["brightness"]:
                self.api_brightness(original["brightness"])
        self.home()
        self.record("preferences_restored", appearance=appearance, brightness=brightness)


def counter_value(ui):
    node = ui.find_label(lambda value: value.isdecimal(), backward=True)
    return int(node["text"])


def appearance(ui, rounds):
    for index in range(rounds):
        ui.open_app("ouse.counter")
        expected = counter_value(ui) + 1
        ui.tap_label("+1")
        require(counter_value(ui) == expected, "Counter increment failed")
        ui.home()
        for font in (100, 115, 130):
            ui.open_app("ouse.api-test")
            ui.set_font(font)
            current = ui.state()
            ui.set_theme(1 - current["theme"])
            ui.set_locale(1 - current["locale"])
            state = ui.state()
            require(state["font"] == font, "Text-size preference changed unexpectedly")
            ui.home()
            ui.open_app("ouse.counter")
            require(counter_value(ui) == expected, "Counter state was lost across preferences")
            ui.find_label("Reset" if state["locale"] else "清零")
            ui.record("appearance_variant", round=index + 1, state=state, counter=expected)
            ui.home()


def lifecycle(ui, rounds):
    for index in range(rounds):
        ui.open_app("ouse.counter")
        ui.tap_label("+1")
        before = ui.state()["live"]
        ui.overview()
        title = ui.find_label(("计数器", "Counter"), scroll=False)
        cards = [parent for parent in reversed(title["_parents"])
                 if parent["clickable"] and parent["bounds"][2] - parent["bounds"][0] > ui.width // 2 and
                 parent["bounds"][3] - parent["bounds"][1] > ui.height // 3]
        require(cards, "Could not identify the recent-app card")
        x1, y1, x2, y2 = cards[0]["bounds"]
        x, y = (x1 + x2) // 2, (y1 + y2) // 2
        ui.device.swipe(x, y, x, max(ui.dp(48), y - ui.dp(120)), 200)
        state = ui.wait_state(lambda s: s["live"] == before - 1 and s["recents"], "Card swipe did not close one instance")
        ui.home()
        ui.open_app("ouse.counter")
        require(counter_value(ui) == 0, "Closed counter did not restart at zero")
        ui.overview()
        ui.tap_label(("全部清除", "Clear all"))
        final = ui.wait_state(lambda s: s["live"] == 0 and not s["active"] and not s["recents"], "Clear all left live instances")
        ui.record("lifecycle_round", round=index + 1, after_card_close=state, final=final)


def resource_data(ui):
    node = ui.find_label(lambda value: value.startswith("RAM:") and "PSRAM:" in value, backward=True)
    text = node["text"]
    ram = re.search(r"RAM:\s*(\d+)\s*/\s*(\d+)\s*KiB", text)
    psram = re.search(r"PSRAM:\s*(\d+)\s*/\s*(\d+)\s*KiB", text)
    uptime = re.search(r"(?:运行时间|Uptime):\s*(\d+)\s*s", text)
    require(ram is not None and psram is not None and uptime is not None,
            "Resource monitor has incomplete telemetry: " + text)
    values = {"ram_free": int(ram.group(1)), "ram_total": int(ram.group(2)),
              "psram_free": int(psram.group(1)), "psram_total": int(psram.group(2)), "uptime": int(uptime.group(1))}
    require(0 < values["ram_free"] <= values["ram_total"] and 0 < values["psram_free"] <= values["psram_total"],
            "Resource monitor returned invalid memory totals")
    return values


def resources(ui, rounds):
    # This is the production JSON/JS monitor backed by the native system API.
    ui.open_app("ouse.resources")
    for index in range(rounds):
        before = resource_data(ui)
        time.sleep(1.25)
        deadline = time.monotonic() + 5
        while True:
            after = resource_data(ui)
            if after["uptime"] > before["uptime"]:
                break
            require(time.monotonic() < deadline, "Resource monitor uptime did not advance")
            time.sleep(.25)
        ui.home()
        time.sleep(.3)
        ui.open_app("ouse.resources")
        resumed = resource_data(ui)
        require(resumed["uptime"] >= after["uptime"], "Resource data regressed after resume")
        ui.record("resources_round", round=index + 1, before=before, after=after, resumed=resumed)
    ui.drag_slider(ui.find_slider(), 3, 1, 5)
    ui.find_label(("刷新间隔: 3000 ms", "Refresh interval: 3000 ms"))
    before = resource_data(ui)
    time.sleep(.5)
    unchanged = resource_data(ui)
    require(unchanged["uptime"] == before["uptime"], "Three-second interval kept refreshing at the old rate")
    time.sleep(3.1)
    after = resource_data(ui)
    require(after["uptime"] > before["uptime"], "Three-second interval stopped refreshing")
    ui.drag_slider(ui.find_slider(), 1, 1, 5)
    ui.find_label(("刷新间隔: 1000 ms", "Refresh interval: 1000 ms"))
    ui.record("resources_interval", seconds=3, before=before, unchanged=unchanged, after=after, restored_seconds=1)
    ui.home()


def quick(ui, rounds):
    for index in range(rounds):
        ui.home()
        state = ui.device.swipe(ui.width // 2, ui.dp(8), ui.width // 2,
                                min(ui.dp(300), ui.height - ui.dp(80)), 200)
        require(state["quick"], "Pull-down did not open quick settings")
        for target in (30, 70):
            title = ui.find_label(("屏幕亮度", "Brightness"))
            title_y = title["bounds"][1]
            light_parent = title["_parents"][-1]["bounds"]
            sliders = [n for n in ui.tree() if n["type"] == "slider" and ui.clip(n) and
                       any(p["bounds"] == light_parent for p in n["_parents"])]
            require(len(sliders) == 1, "Brightness slider was not uniquely identifiable")
            state = ui.drag_slider(sliders[0], target, 10, 100)
            require(state["quick"] and not state["tracking"] and not state["active"],
                    "Slider drag started a panel/navigation gesture")
            title_after = ui.find_label(("屏幕亮度", "Brightness"), scroll=False)
            require(abs(title_after["bounds"][1] - title_y) <= 2, "Slider drag displaced quick settings")
            labels = [n["text"] for n in ui.tree() if n["type"] == "label" and
                      any(p["bounds"] == light_parent for p in n["_parents"])]
            require(f"{target}%" in labels, "Quick brightness value did not match requested drag")
        ui.home()
        require(ui.read_brightness() == 70, "Quick brightness was not reflected in the system API")
        ui.home()
        ui.record("quick_round", round=index + 1, brightness=70)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--group", choices=("all", "appearance", "lifecycle", "resources", "quick"), default="all")
    parser.add_argument("--rounds", type=int, default=3)
    parser.add_argument("--port", default="COM10")
    parser.add_argument("--elf")
    parser.add_argument("--output")
    args = parser.parse_args()
    if not 1 <= args.rounds <= 100:
        parser.error("--rounds must be 1..100")

    report = {"group": args.group, "rounds_requested": args.rounds, "started": time.time(),
              "status": "running", "events": [], "groups": []}
    device, ui, original = None, None, None
    dirty_appearance = dirty_brightness = False
    code = 0

    def save():
        if device is not None:
            (device.output / "system-result.json").write_text(
                json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")

    try:
        device = Device(args.port, args.output, args.elf)
        report["output"] = str(device.output.resolve())
        require("debug=1" in device.request("HELLO", timeout=8), "Device debug controls are unavailable")
        ui = UI(device, report, save)
        initial = ui.state()
        original = {key: initial[key] for key in ("theme", "locale", "font")}
        original["brightness"] = ui.read_brightness()
        report["initial"] = initial
        report["original_preferences"] = original
        save()
        groups = ("appearance", "resources", "quick", "lifecycle") if args.group == "all" else (args.group,)
        operations = {"appearance": appearance, "lifecycle": lifecycle, "resources": resources, "quick": quick}
        for name in groups:
            ui.checkpoint(name + "_before")
            if name == "appearance":
                dirty_appearance = dirty_brightness = True
            elif name == "quick":
                dirty_brightness = True
            group_error = None
            try:
                operations[name](ui, args.rounds)
            except (Exception, KeyboardInterrupt) as error:
                group_error = error
                report["group_error"] = {"group": name, "type": type(error).__name__, "message": str(error)}
                raise
            finally:
                if (dirty_appearance or dirty_brightness) and ui.alive():
                    try:
                        ui.restore_preferences(original, dirty_appearance, dirty_brightness)
                        dirty_appearance = dirty_brightness = False
                    except Exception as error:
                        report["restore_error"] = str(error)
                        if group_error is None:
                            raise
            ui.checkpoint(name + "_after")
            report["groups"].append({"group": name, "rounds": args.rounds, "passed": True})
            save()
            print(f"PASS system UI {name}: {args.rounds} rounds", flush=True)
        report["final"] = ui.state()
        report["status"] = "passed"
    except (Exception, KeyboardInterrupt) as error:
        report["status"] = "interrupted" if isinstance(error, KeyboardInterrupt) else "failed"
        report["error"] = {"type": type(error).__name__, "message": str(error)}
        code = 130 if isinstance(error, KeyboardInterrupt) else 1
        print("System UI test failed:", error, file=sys.stderr, flush=True)
    finally:
        if device is not None:
            if ui is not None and original is not None and (dirty_appearance or dirty_brightness) and ui.alive():
                try:
                    ui.restore_preferences(original, dirty_appearance, dirty_brightness)
                except Exception as error:
                    report["restore_error"] = str(error)
                    code = 1
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
