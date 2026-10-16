"""Exercise real edge, overview and quick-sheet gestures on the actual board.

Only application opens use the debug open command. Navigation itself uses the
production pointer input and gesture timer. No packages, credentials or system
preferences are changed. Screenshots document intermediate and settled UI.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import sys
import time

from device import Device

spec = importlib.util.spec_from_file_location("ouse_device_system", Path(__file__).with_name("test-device-system.py"))
system = importlib.util.module_from_spec(spec)
spec.loader.exec_module(system)
UI, require = system.UI, system.require


class Navigation:
    def __init__(self, ui):
        self.ui = ui
        self.images = set()

    def image(self, name):
        # One image per distinct condition avoids spending every test round
        # transferring the same JPEG over the control connection.
        if name in self.images:
            return
        path = self.ui.device.screenshot(self.ui.device.output / (name + ".jpg"))
        self.images.add(name)
        self.ui.record("screenshot", name=name, path=str(path))

    def release(self, x, y):
        if self.ui.alive():
            self.ui.device.touch(x, y, False)

    def move(self, x, y, end_x, end_y, duration=.15):
        self.ui.device.touch(x, y, True)
        for step in range(1, 5):
            started = time.monotonic()
            self.ui.device.touch(round(x + (end_x - x) * step / 4),
                                 round(y + (end_y - y) * step / 4), True)
            time.sleep(max(0, duration / 4 - (time.monotonic() - started)))

    def has_settings_back(self, nodes=None):
        nodes = nodes if nodes is not None else self.ui.tree()
        return any(n["type"] == "label" and n["text"] in ("返回设置", "Settings", "\uf053") and
                   any(p["type"] == "button" for p in n["_parents"]) for n in nodes)

    def settings_root(self):
        self.ui.open_app("ouse.settings")
        if self.has_settings_back():
            self.ui.tap_label(("返回设置", "Settings", "\uf053"), backward=True)
        self.ui.find_label(("显示与个性化", "Personalization"), backward=True)
        require(not self.has_settings_back(), "Settings root still has a child-page Back control")

    def edge(self, left, distance, image=None):
        ui = self.ui
        x = ui.dp(4) if left else ui.width - ui.dp(4)
        y = ui.height // 2
        end_x = x + ui.dp(distance) * (1 if left else -1)
        try:
            self.move(x, y, end_x, y)
            during = ui.wait_state(lambda s: s["tracking"], "Edge gesture did not claim the pointer")
            if image:
                self.image(image)
            ui.record("edge_contact", left=left, distance_dp=distance, during=during)
        finally:
            self.release(end_x, y)
        return ui.wait_state(lambda s: not s["tracking"], "Edge release left a captured pointer")

    def snapped(self):
        ui = self.ui
        deadline = time.monotonic() + 6
        previous, stable = None, 0
        while True:
            state = ui.state()
            require(state["recents"] and state["cards"] >= 3 and not state["active"],
                    "Overview lost an application while horizontally scrolling: " + repr(state))
            position = state["scroll_x"]
            stable = stable + 1 if position == previous else 0
            spacing = ui.dp(244)
            aligned = abs(position - round(position / spacing) * spacing) <= 2
            if stable >= 3 and aligned and not state["tracking"]:
                return state
            require(time.monotonic() < deadline, "Recent-app strip did not settle on a card: " + repr(state))
            previous = position
            time.sleep(.12)

    def centered_card(self):
        ui = self.ui
        nodes = ui.tree()
        candidates = [n for n in nodes if n["clickable"] and n["type"] == "object" and
                      abs(n["bounds"][2] - n["bounds"][0] + 1 - ui.dp(224)) <= 2 and
                      abs(n["bounds"][3] - n["bounds"][1] + 1 - (ui.height - ui.dp(170))) <= 2 and
                      abs((n["bounds"][0] + n["bounds"][2]) / 2 - ui.width / 2) <= 3]
        require(len(candidates) == 1, "Could not identify exactly one centered recent-app card")
        card = candidates[0]
        title_nodes = [n for n in nodes if n["type"] == "label" and any(
            p["bounds"] == card["bounds"] and p["depth"] == card["depth"] for p in n["_parents"])]
        mapping = {"设置": "ouse.settings", "Settings": "ouse.settings", "计数器": "ouse.counter", "Counter": "ouse.counter",
                   "资源监视": "ouse.resources", "Resources": "ouse.resources", "接口测试": "ouse.api-test", "API test": "ouse.api-test"}
        titles = [n["text"] for n in title_nodes if n["text"] in mapping]
        require(len(titles) == 1, "Centered card's app title is missing or ambiguous: " + repr(title_nodes))
        return card, mapping[titles[0]]

    def sheet_y(self):
        ui = self.ui
        sheets = [n for n in ui.tree() if n["type"] == "object" and
                  abs(n["bounds"][2] - n["bounds"][0] + 1 - ui.width) <= 2 and
                  abs(n["bounds"][3] - n["bounds"][1] + 1 - ui.height) <= 2 and
                  -ui.height < n["bounds"][1] < 0 <= n["bounds"][3]]
        require(len(sheets) == 1, "Could not identify the partly exposed full-height quick sheet")
        return sheets[0]["bounds"][1]


def edges(nav, rounds):
    ui = nav.ui
    for index in range(rounds):
        for left in (True, False):
            nav.settings_root()
            ui.tap_label(("显示", "Display"))
            require(nav.has_settings_back(), "Display settings did not expose its Back control")
            cancelled = nav.edge(left, 20, "back-left-follow" if left else "back-right-follow")
            require(cancelled["active"] == "ouse.settings" and nav.has_settings_back(),
                    "A short edge drag incorrectly left the settings child page")
            child_back = nav.edge(left, 80)
            require(child_back["active"] == "ouse.settings" and not child_back["recents"] and not child_back["quick"],
                    "Settings child-page Back should remain in Settings")
            ui.find_label(("显示与个性化", "Personalization"), backward=True)
            require(not nav.has_settings_back(), "Edge Back did not return to the Settings root")
            root_back = nav.edge(left, 80)
            require(not root_back["active"] and not root_back["recents"] and not root_back["quick"],
                    "Settings-root Back did not reach the desktop")
            ui.record("edge_round", round=index + 1, left=left, short_cancel=cancelled,
                      child_back=child_back, root_back=root_back)
        print(f"PASS both edges: {index + 1}/{rounds}", flush=True)


def overview(nav, rounds):
    ui = nav.ui
    for index in range(rounds):
        for app_id in ("ouse.settings", "ouse.counter", "ouse.resources"):
            ui.open_app(app_id)
            ui.home()
        ui.open_app("ouse.counter")
        before_counter = system.counter_value(ui)
        # Continuous upward movement followed by immediate release goes home.
        fast = ui.device.swipe(ui.width // 2, ui.height - ui.dp(6), ui.width // 2,
                               ui.height - ui.dp(100), 180)
        require(not fast["active"] and not fast["recents"] and not fast["tracking"],
                "Fast bottom swipe incorrectly opened overview")
        nav.image("fast-swipe-desktop")
        ui.open_app("ouse.counter")
        x, start_y, end_y = ui.width // 2, ui.height - ui.dp(6), ui.height - ui.dp(100)
        try:
            nav.move(x, start_y, x, end_y)
            # The 160 ms recognition timer must work without fresh samples.
            time.sleep(.24)
            held = ui.wait_state(lambda s: s["recents"] and s["tracking"], "Stationary hold did not open overview")
            require(held["cards"] >= 3 and not held["active"], "Overview did not retain at least three instances")
            nav.image("overview-held")
        finally:
            nav.release(x, end_y)
        initial = nav.snapped()
        require(initial["scroll_x"] == 0, "New overview did not start on its first card")
        first_card, first_id = nav.centered_card()
        require(first_id == "ouse.counter", "The newest app was not the first recent card")

        y = min(ui.height // 2, ui.height - ui.dp(190))
        ui.device.swipe(ui.width - ui.dp(85), y, ui.dp(65), y, 230)
        next_card = nav.snapped()
        require(next_card["scroll_x"] > initial["scroll_x"] and next_card["cards"] == initial["cards"],
                "Leftward drag did not reveal another recent app")
        nav.image("overview-scrolled-left")
        card, target_id = nav.centered_card()
        require(target_id != first_id, "Horizontal scrolling still shows the first app")
        a, b, c, d = card["bounds"]
        ui.device.tap((a + c) // 2, (b + d) // 2)
        resumed = ui.wait_state(lambda s: s["active"] == target_id and not s["recents"], "Tapping the recent card did not restore its app")
        require(resumed["live"] == initial["live"], "Restoring a recent app created or lost an instance")
        nav.image("overview-card-restored")

        ui.overview()
        nav.snapped()
        ui.device.swipe(ui.width - ui.dp(85), y, ui.dp(65), y, 230)
        left = nav.snapped()
        require(left["scroll_x"] > 0, "Repeated horizontal scroll did not move the card strip")
        for _ in range(left["cards"] + 2):
            ui.device.swipe(ui.dp(65), y, ui.width - ui.dp(85), y, 230)
            right = nav.snapped()
            if right["scroll_x"] == 0:
                break
        require(right["scroll_x"] == 0 and right["cards"] == initial["cards"],
                "Rightward drags did not return to the first snap position")
        nav.image("overview-scrolled-right")
        ui.device.tap(ui.dp(36), ui.height - ui.dp(70))
        blank = ui.wait_state(lambda s: not s["active"] and not s["recents"] and not s["tracking"],
                              "Tapping blank overview space did not return home")
        require(blank["live"] == initial["live"], "Blank-space dismissal closed a recent app")
        ui.open_app("ouse.counter")
        require(system.counter_value(ui) == before_counter, "Navigation lost the running Counter state")
        ui.home()
        ui.record("overview_round", round=index + 1, fast=fast, held=held, initial=initial,
                  left=next_card, resumed=resumed, right=right, blank=blank, counter=before_counter)
        print(f"PASS held overview, three cards, horizontal snap/restore/blank: {index + 1}/{rounds}", flush=True)


def quick(nav, rounds):
    ui = nav.ui
    for index in range(rounds):
        ui.home()
        x, start_y = ui.width // 2, ui.dp(8)
        # A short pull exposes the sheet, then returns it completely offscreen.
        short_end = ui.dp(42)
        try:
            nav.move(x, start_y, x, short_end)
            during_short = ui.wait_state(lambda s: s["quick"] and s["tracking"], "Short quick pull did not follow the finger")
            short_y = nav.sheet_y()
            require(abs(short_y - (-ui.height + short_end - start_y)) <= 3, "Short quick pull has the wrong offset")
            nav.image("quick-short-follow")
        finally:
            nav.release(x, short_end)
        cancelled = ui.wait_state(lambda s: not s["quick"] and not s["tracking"], "Short quick pull did not rebound closed")

        first_y, second_y, full_end = ui.dp(70), ui.dp(150), ui.dp(230)
        try:
            nav.move(x, start_y, x, first_y)
            ui.wait_state(lambda s: s["quick"] and s["tracking"], "Full quick pull did not start tracking")
            first_sheet_y = nav.sheet_y()
            ui.device.touch(x, second_y, True)
            time.sleep(.06)  # allow the real indev and gesture timers to sample
            second_sheet_y = nav.sheet_y()
            require(abs((second_sheet_y - first_sheet_y) - (second_y - first_y)) <= 3,
                    "Quick sheet did not move by the same distance as the finger")
            require(abs(second_sheet_y - (-ui.height + second_y - start_y)) <= 3, "Quick sheet jumped away from the contact")
            nav.image("quick-full-follow")
            ui.device.touch(x, full_end, True)
            time.sleep(.04)
        finally:
            nav.release(x, full_end)
        opened = ui.wait_state(lambda s: s["quick"] and not s["tracking"], "Full quick pull did not settle open")
        # The completion animation lasts 180 ms; find_label uses the shared
        # geometry-aware helper before capturing the final open appearance.
        time.sleep(.3)
        ui.find_label(("屏幕亮度", "Brightness"), scroll=False)
        nav.image("quick-open")
        closed = ui.device.swipe(x, ui.dp(75), x, ui.dp(2), 200)
        closed = ui.wait_state(lambda s: not s["quick"] and not s["tracking"], "Upward quick-sheet drag did not dismiss it")
        require(not closed["active"] and not closed["recents"], "Dismissing quick settings left an unwanted app or overview")
        ui.record("quick_navigation_round", round=index + 1, short_follow=during_short, short_y=short_y,
                  cancelled=cancelled, first_sheet_y=first_sheet_y, second_sheet_y=second_sheet_y,
                  opened=opened, closed=closed)
        print(f"PASS quick-sheet follow, short rebound, open and dismiss: {index + 1}/{rounds}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--group", choices=("all", "edges", "overview", "quick"), default="all")
    parser.add_argument("--rounds", type=int, default=3)
    parser.add_argument("--skip-unity", action="store_true", help="Use when the same firmware's five board tests have already passed")
    parser.add_argument("--port", default="COM10")
    parser.add_argument("--elf")
    parser.add_argument("--output")
    args = parser.parse_args()
    if not 1 <= args.rounds <= 50:
        parser.error("--rounds must be 1..50")
    report = {"group": args.group, "rounds_requested": args.rounds, "status": "running", "started": time.time(), "events": [], "groups": []}
    device = ui = nav = None
    code = 0

    def save():
        if device is not None:
            (device.output / "navigation-result.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    try:
        device = Device(args.port, args.output, args.elf)
        report["output"] = str(device.output.resolve())
        require("debug=1" in device.request("HELLO", timeout=8), "Device debug controls are unavailable")
        ui = UI(device, report, save)
        nav = Navigation(ui)
        report["initial"] = ui.state()
        save()
        if not args.skip_unity:
            report["unity"] = device.tests()
            require(report["unity"]["tests"] == 5 and report["unity"]["failures"] == 0, "On-device gesture tests failed")
            save()
        groups = ("edges", "overview", "quick") if args.group == "all" else (args.group,)
        operations = {"edges": edges, "overview": overview, "quick": quick}
        for group in groups:
            ui.checkpoint(group + "_before")
            operations[group](nav, args.rounds)
            ui.checkpoint(group + "_after")
            report["groups"].append({"group": group, "rounds": args.rounds, "passed": True})
            save()
        report["final"] = ui.state()
        report["status"] = "passed"
    except (Exception, KeyboardInterrupt) as error:
        report["status"] = "interrupted" if isinstance(error, KeyboardInterrupt) else "failed"
        report["error"] = {"type": type(error).__name__, "message": str(error)}
        code = 130 if isinstance(error, KeyboardInterrupt) else 1
        print("Navigation UI test failed:", error, file=sys.stderr, flush=True)
    finally:
        if device is not None:
            if ui is not None and ui.alive():
                try:
                    if device.contact:
                        nav.release(*device.contact)
                    if report["status"] != "passed":
                        nav.image("navigation-failure")
                    ui.home()
                except Exception as error:
                    report["cleanup_error"] = str(error)
                    report["status"] = "failed"
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
