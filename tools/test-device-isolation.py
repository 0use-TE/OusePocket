"""Run one isolated display experiment on the board, without snapshots or app switching.

Restart the board between phases. Desktop and settings use native C/LVGL UI;
counter additionally exercises the existing JSON/JS app and its increment event.
"""
import argparse
import json
import sys
import time

from device import Device


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def check_state(state, active, quick=False):
    require(state["active"] == active, "Unexpected foreground app: " + repr(state))
    require(bool(state["quick"]) == quick, "Unexpected quick-panel state: " + repr(state))
    require(not state["recents"] and not state["tracking"],
            "Unexpected gesture or overview: " + repr(state))


def counter_value(tree):
    values = [int(node["text"]) for node in tree["nodes"]
              if node["type"] == "label" and node["text"].isdecimal()]
    require(len(values) == 1, "Counter value was not uniquely identifiable")
    return values[0]


def run_phase(device, args, report, save):
    initial = report["initial"]
    target = {"desktop": "", "settings": "ouse.settings", "counter": "ouse.counter"}[args.phase]
    require(initial["active"] in ("", target),
            "Restart the device before this phase; another app is foreground: " + initial["active"])
    require(not initial["quick"] and not initial["recents"] and not initial["tracking"],
            "Restart the device before this phase; a panel or gesture is active")
    expected_live = 1 if initial["active"] else 0
    require(initial["live"] <= expected_live,
            "Restart the device before this phase; other app instances are still alive")

    width, height = initial["width"], initial["height"]
    density = initial["dpi"]
    require(width >= 240 and height >= 320 and 80 <= density <= 320,
            "Unsupported geometry for safe isolation gestures")

    def dp(value):
        return round(value * density / 160)

    if target and not initial["active"]:
        # With no foreground app, this does not take an application preview.
        device.request("DBG_OPEN", target, timeout=10, attempts=1)
        time.sleep(.2)
    state = device.state()
    check_state(state, target)
    report["opened"] = state
    save()

    increment = None
    if args.phase == "counter":
        # Two small UI-tree reads locate the real button and verify the result.
        # Desktop/settings do not allocate or export debug resources at all.
        tree = device.tree()
        labels = [node for node in tree["nodes"]
                  if node["type"] == "label" and node["text"] == "+1"]
        require(len(labels) == 1, "Counter increment button was not uniquely identifiable")
        x1, y1, x2, y2 = labels[0]["bounds"]
        increment = ((x1 + x2) // 2, (y1 + y2) // 2)
        require(dp(24) < increment[0] < width - dp(24) and
                dp(40) < increment[1] < height - dp(36),
                "Counter button is outside the safe central input region")
        report["counter_before"] = counter_value(tree)
        report["increment_point"] = increment
        save()

    for index in range(args.rounds):
        if args.phase == "desktop":
            state = device.swipe(width // 2, dp(8), width // 2,
                                 min(dp(300), height - dp(80)), 180)
            check_state(state, "", quick=True)
            state = device.swipe(width // 2, dp(80), width // 2, dp(4), 180)
            check_state(state, "")
        elif args.phase == "settings":
            bottom = height - dp(60)
            top = max(dp(60), min(height // 2, dp(200)))
            require(bottom - top > dp(48), "Display is too short for safe settings scrolling")
            state = device.swipe(width // 2, bottom, width // 2, top, 180)
            check_state(state, target)
            state = device.swipe(width // 2, top, width // 2, bottom, 180)
            check_state(state, target)
        else:
            state = device.tap(*increment)
            check_state(state, target)
        report["rounds"].append({"round": index + 1, "time": time.time(), "state": state})
        save()
        print(f"Isolation {args.phase}: {index + 1}/{args.rounds} passed", flush=True)

    if args.phase == "counter":
        report["counter_after"] = counter_value(device.tree())
        require(report["counter_after"] == report["counter_before"] + args.rounds,
                "Counter did not receive every increment event")
    report["final"] = device.state()
    check_state(report["final"], target)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--phase", choices=("desktop", "settings", "counter"), required=True,
                        help="Run exactly one phase; restart the board between experiments")
    parser.add_argument("--rounds", type=int, default=20)
    parser.add_argument("--busy-idle", action="store_true",
                        help="Diagnostic A/B: bypass CPU WFI while leaving all services enabled")
    parser.add_argument("--port", default="COM10")
    parser.add_argument("--elf")
    parser.add_argument("--output")
    args = parser.parse_args()
    if not 1 <= args.rounds <= 2000:
        parser.error("--rounds must be between 1 and 2000")

    report = {"phase": args.phase, "busy_idle": args.busy_idle,
              "rounds_requested": args.rounds, "rounds": [],
              "started": time.time(), "status": "running"}
    device = None
    idle_requested = False
    exit_code = 0

    def save():
        if device is not None:
            (device.output / "isolation-result.json").write_text(
                json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")

    try:
        device = Device(args.port, args.output, args.elf)
        report["output"] = str(device.output.resolve())
        save()
        # A request sent before UART installation during boot can be lost;
        # retry the same idempotent HELLO after the service becomes ready.
        hello = device.request("HELLO", timeout=8, attempts=3)
        require("debug=1" in hello, "Device firmware does not provide debug controls")
        report["hello"] = hello
        # Establish the requested mode explicitly, even if an earlier run stopped.
        # Mark before sending so a lost acknowledgement still triggers restoration.
        idle_requested = True
        report["idle_mode"] = device.request("DBG_IDLE", "0" if args.busy_idle else "1", attempts=1)
        report["initial"] = device.state()
        save()
        run_phase(device, args, report, save)
        report["status"] = "passed"
    except (Exception, KeyboardInterrupt) as error:
        report["status"] = "interrupted" if isinstance(error, KeyboardInterrupt) else "failed"
        report["error"] = {"type": type(error).__name__, "message": str(error)}
        exit_code = 130 if isinstance(error, KeyboardInterrupt) else 1
        print(f"Isolation {args.phase} {report['status']}: {error}", file=sys.stderr, flush=True)
    finally:
        if device is not None:
            healthy = not device.capture.panic and not device.rebooted and device.error is None
            if idle_requested and healthy:
                try:
                    report["idle_restored"] = device.request("DBG_IDLE", "1", timeout=3, attempts=1)
                except Exception as error:
                    report["idle_restore_error"] = str(error)
                    if report["status"] == "passed":
                        report["status"] = "failed"
                        exit_code = 1
            elif idle_requested:
                report["idle_restore_skipped"] = "panic, reboot, or serial failure; no further commands sent"
            save()
            # Device owns the UART reader and waits for any complete dump/reboot.
            try:
                device.close()
            except Exception as error:
                report["capture_close_error"] = str(error)
                if report["status"] == "passed":
                    report["status"] = "failed"
                    exit_code = 1
            report["panic_seen"] = bool(device.capture.panic)
            report["unexpected_reboot"] = device.rebooted
            if report["panic_seen"] or report["unexpected_reboot"]:
                report["status"] = "failed"
                exit_code = 1
            report["finished"] = time.time()
            save()
            print("Evidence:", device.output.resolve(), flush=True)
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
