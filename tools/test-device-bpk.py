"""Regress real-board BPK install/update/rejection/abort/timeout/removal with one UART owner.

Only user.stability-test is created or removed. An existing package with that ID
is a hard precondition failure; all other installed packages remain untouched.
The real upload-expiry test takes about 30 seconds without transmitting commands.
Use --verify-reboot to additionally interrupt a partial update with a CH343
RTS/EN reset and verify the committed version restores. No flashing or erasing occurs.
Fixtures use the sole BPK format: manifest/page/app/icon.bin (OIC1 BGRA).
"""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import sys
import time
import traceback

import bpk
from device import Device


APP_ID = "user.stability-test"
FAULT_SCRIPTS = {
    "event-throw": "Ouse.ui.on('increment','click',()=>{throw new Error('event-throw test');});",
    "event-loop": "Ouse.ui.on('increment','click',()=>{while(true){}});",
    "pause-throw": "globalThis.app={onPause(){throw new Error('pause-throw test');}};",
    "pause-loop": "globalThis.app={onPause(){while(true){}}};",
}


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def healthy(device):
    return not device.capture.panic and not device.rebooted and device.error is None


def installed_ids(device):
    text = device.request("LIST", timeout=8)
    ids = [] if text == "-" else text.split(",")
    require(len(ids) == len(set(ids)), "LIST contains duplicate application IDs")
    return sorted(ids)


def build_packages(directory, include_faults=False):
    directory.mkdir(parents=True, exist_ok=True)
    packages = {}
    versions = ["v1", "v2", "v3", "bad-schema"]
    if include_faults:
        versions.extend(FAULT_SCRIPTS)
    for version in versions:
        source = directory / version
        source.mkdir()
        manifest = dict(runtime="ouse-core", api=1, id=APP_ID,
                        name="Stability Test", icon="icon.bin",
                        names={"zh_CN": "稳定性测试", "en": "Stability Test"})
        page = {"api": 1, "units": "dp", "root": {
            "type": "container", "id": "page", "width": "fill", "height": "fill",
            "layout": "column", "padding": 12, "gap": 12,
            "children": [
                {"type": "label", "id": "version", "width": "fill", "height": 36,
                 "text": "Stability " + version, "fontSize": 22},
                {"type": "label", "id": "value", "width": "fill", "height": 32,
                 "text": "count=0", "fontSize": 20},
                {"type": "button", "id": "increment", "width": "fill", "height": 48,
                 "text": "+1"},
            ]}}
        if version == "bad-schema":
            # Valid ZIP/manifest/JSON, deliberately rejected by the real runtime
            # schema validator. bpk.pack's envelope validation still succeeds.
            page["root"]["children"][0]["type"] = "invalid-widget"
        script = ("'use strict';let count=0;"
                  "Ouse.ui.on('increment','click',()=>{++count;"
                  "Ouse.ui.setText('value','count='+count);});")
        script += FAULT_SCRIPTS.get(version, "")
        (source / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
        (source / "page.json").write_text(json.dumps(page), encoding="utf-8")
        (source / "app.js").write_text(script, encoding="utf-8")
        (source / "icon.bin").write_bytes(b"OIC1" + struct.pack("<HH", 16, 16) +
                                          bytes((0xC0, 0x78, 0x30, 0xFF)) * (16 * 16))
        archive = directory / (version + ".bpk")
        require(bpk.pack(source, archive)["id"] == APP_ID, "Unexpected generated BPK ID")
        data = archive.read_bytes()
        require(bpk.validate(data)["id"] == APP_ID, "Unexpected validated BPK ID")
        packages[version] = data
    return packages


def upload(device, data, checksum=None):
    checksum = bpk.crc(data) if checksum is None else checksum
    device.request("BEGIN", f"{len(data):08x}:{checksum:08x}", timeout=8)
    try:
        for offset in range(0, len(data), 512):
            chunk = data[offset:offset + 512]
            result = device.request("DATA", f"{offset:08x}:{chunk.hex()}", timeout=8)
            require(int(result) == offset + len(chunk), "DATA acknowledged an incorrect offset")
        return device.request("END", timeout=20)
    except BaseException:
        # The firmware releases END failures, but a failed DATA can leave an
        # upload active. Abort only while healthy; never disrupt a panic dump.
        if healthy(device):
            try:
                device.request("ABORT", timeout=3, attempts=1)
            except Exception:
                pass
        raise


def expect_rejected(call, verb, code="ESP_ERR_INVALID_ARG"):
    try:
        call()
    except RuntimeError as error:
        require(str(error) == verb + ": " + code,
                "Expected a protocol rejection, got: " + repr(error))
        return str(error)
    raise RuntimeError(verb + " unexpectedly accepted an invalid operation")


def check_app(device, version, count, output, name):
    state = device.state()
    require(state["active"] == APP_ID, "Test app is not foreground: " + repr(state))
    require(not state["quick"] and not state["recents"] and not state["tracking"],
            "Unexpected overlay/gesture: " + repr(state))
    tree = device.tree()
    (output / (name + "-tree.json")).write_text(json.dumps(tree, indent=2), encoding="utf-8")
    texts = [node["text"] for node in tree["nodes"] if node["type"] == "label"]
    require("Stability " + version in texts, "Expected BPK version is absent: " + repr(texts))
    require("count=" + str(count) in texts, "Application state was lost: " + repr(texts))
    return state, tree


def increment(device, tree):
    labels = [node for node in tree["nodes"] if node["type"] == "label" and node["text"] == "+1"]
    require(len(labels) == 1, "Test increment button is not uniquely identifiable")
    x1, y1, x2, y2 = labels[0]["bounds"]
    device.tap((x1 + x2) // 2, (y1 + y2) // 2)


def begin_partial_upload(device, data):
    device.request("BEGIN", f"{len(data):08x}:{bpk.crc(data):08x}")
    chunk = data[:min(512, len(data) - 1)]
    require(0 < len(chunk) < len(data), "Fixture must be an incomplete upload")
    require(int(device.request("DATA", "00000000:" + chunk.hex())) == len(chunk),
            "Partial DATA acknowledged an incorrect offset")
    return len(chunk)


def wait_for_upload_timeout(device, timeout=45):
    # The real consume_bytes() expires activity after >30,000,000 us; its UART
    # worker checks at one-second read deadlines. Never send HELLO/state/debug
    # while waiting: every accepted command refreshes that activity timestamp.
    marker = b"Upload timeout; previous app preserved"
    start = device.capture.bytes
    started = time.monotonic()
    pending = b""
    with (device.output / "uart.raw.log").open("rb") as raw:
        raw.seek(start)
        while time.monotonic() - started < timeout:
            require(healthy(device), "Board failed while observing the real upload timeout")
            pending += raw.read()
            if marker in pending:
                return dict(threshold_seconds=30, observed_seconds=time.monotonic() - started,
                            raw_offset=start, marker=marker.decode("ascii"))
            pending = pending[-len(marker):]
            time.sleep(.2)
    raise TimeoutError("No firmware upload-timeout event observed within " + str(timeout) + " seconds")


def check_no_upload(device):
    # Confirmed against command()/install_upload(): DATA without upload returns
    # INVALID_STATE; END routes to install_upload() and returns INVALID_ARG;
    # ABORT is idempotent, even when there is no upload to discard.
    data_error = expect_rejected(lambda: device.request("DATA", "00000000:00"),
                                 "DATA", "ESP_ERR_INVALID_STATE")
    end_error = expect_rejected(lambda: device.request("END"), "END")
    abort = device.request("ABORT")
    require(abort == "-", "ABORT returned an unexpected payload")
    return dict(data_rejection=data_error, end_rejection=end_error, abort=abort)


def reboot_and_restore(device):
    require(healthy(device), "Refusing to reset a failed board or interrupt a panic dump")
    # Match esptool's CH343 hard-reset sequence through the already-owned port.
    # Suppress only the expected ROM boot detector, retaining all UART bytes and
    # the panic detector. Subsequent boot detection is re-armed by HELLO.
    start = device.capture.bytes
    device.ready = False
    try:
        device.port.setRTS(True)
        device.port.setDTR(device.port.dtr)
        time.sleep(.1)
    finally:
        device.port.setRTS(False)
        device.port.setDTR(device.port.dtr)
    hello = device.request("HELLO", timeout=10, attempts=4)
    require("storage=ready" in hello and "debug=1" in hello,
            "Installer did not recover after reset: " + hello)
    with (device.output / "uart.raw.log").open("rb") as raw:
        raw.seek(start)
        boot = raw.read()
    require(b"ESP-ROM:" in boot, "Expected reset produced no ROM boot evidence")
    require(b"Restored " + APP_ID.encode("ascii") in boot,
            "Firmware did not report restoring the persisted test package")
    return dict(hello=hello, raw_offset=start, boot_bytes=len(boot))


def check_fault_panel(device, case):
    state = device.state()
    require(state["active"] == APP_ID, "Faulted app unexpectedly left the foreground")
    tree = device.tree()
    (device.output / (case + "-fault-tree.json")).write_text(json.dumps(tree, indent=2), encoding="utf-8")
    texts = [node["text"] for node in tree["nodes"] if node["type"] == "label"]
    title = "Application error" if state["locale"] else "应用运行出错"
    require(title in texts, "The native runtime error panel is absent: " + repr(texts))
    return state, tree


def native_fault_home(device, tree):
    # This callback is native C and stays operational after the JS instance has
    # faulted. Exercise it with a real touch rather than substituting DBG_HOME.
    buttons = [node for node in tree["nodes"] if node["type"] == "label" and
               node["text"] in ("回到桌面", "Home")]
    require(len(buttons) == 1, "Native error-panel Home button is not uniquely identifiable")
    x1, y1, x2, y2 = buttons[0]["bounds"]
    state = device.tap((x1 + x2) // 2, (y1 + y2) // 2)
    require(state["active"] == "" and not state["recents"] and not state["quick"],
            "Native error-panel Home did not return to the desktop: " + repr(state))
    return state


def native_settings_works(device, case):
    # Settings is the sole native app. Do not depend on a removed built-in
    # counter or on the user's optional installed packages after a JS fault.
    device.request("DBG_OPEN", "ouse.settings", timeout=10)
    require(device.state()["active"] == "ouse.settings", "Native Settings could not be opened")
    # A cached Settings instance may still show the user's last subpage.
    for _ in range(3):
        before = device.tree()
        back = [node for node in before["nodes"] if node["type"] == "label" and node["text"] == "\uf053"]
        if not back:
            break
        require(len(back) == 1, "Native Settings back arrow is not uniquely identifiable")
        x1, y1, x2, y2 = back[0]["bounds"]
        device.tap((x1 + x2) // 2, (y1 + y2) // 2)
    before = device.tree()
    displays = [node for node in before["nodes"] if node["type"] == "label" and
                node["text"] in ("显示", "Display")]
    require(len(displays) == 1, "Native display-settings row is not uniquely identifiable")
    x1, y1, x2, y2 = displays[0]["bounds"]
    device.tap((x1 + x2) // 2, (y1 + y2) // 2)
    after = device.tree()
    require(any(node["type"] == "label" and node["text"] in ("屏幕亮度", "Brightness")
                for node in after["nodes"]), "Native Settings did not receive its navigation event")
    (device.output / (case + "-healthy-settings-tree.json")).write_text(
        json.dumps(after, indent=2), encoding="utf-8")
    state = device.state()
    require(healthy(device) and state["active"] == "ouse.settings",
            "Board did not remain healthy after the JS fault")
    back = [node for node in after["nodes"] if node["type"] == "label" and node["text"] == "\uf053"]
    require(len(back) == 1, "Display-settings back arrow is not uniquely identifiable")
    x1, y1, x2, y2 = back[0]["bounds"]
    device.tap((x1 + x2) // 2, (y1 + y2) // 2)
    device.request("DBG_HOME")
    return dict(opened="ouse.settings", page="display", state=state)


def run_fault_cases(device, report, save, baseline, packages):
    report["fault_cases"] = []
    for case in FAULT_SCRIPTS:
        require(healthy(device), "Refusing further fault inputs after a board failure")
        require(installed_ids(device) == baseline, "Unexpected packages before fault case " + case)
        before = device.state()
        entry = dict(case=case, status="running", started=time.time())
        report["fault_cases"].append(entry)
        save()
        require(upload(device, packages[case]) == APP_ID, "Fault fixture installation failed")
        device.request("OPEN", APP_ID, timeout=10)
        _, tree = check_app(device, case, 0, device.output, case + "-before")
        require(not any(node["text"] in ("应用运行出错", "Application error")
                        for node in tree["nodes"]), "Fault fixture failed before its trigger")
        offset = device.capture.bytes
        started = time.monotonic()
        if case.startswith("event-"):
            increment(device, tree)
        else:
            # Backgrounding invokes onPause. A broken hook must not prevent
            # navigation; its faulted instance remains registered in recents.
            device.request("DBG_HOME", timeout=5, attempts=1)
            require(device.state()["active"] == "", "Broken onPause blocked navigation home")
            device.request("OPEN", APP_ID, timeout=10)
        entry["trigger_round_trip_seconds"] = time.monotonic() - started
        require(entry["trigger_round_trip_seconds"] < 5,
                "Runtime fault recovery exceeded the bounded test deadline")
        state, fault_tree = check_fault_panel(device, case)
        # A runtime error does not uninstall an app. Verify this actual contract
        # before intentionally removing the test package below.
        require(installed_ids(device) == sorted(baseline + [APP_ID]),
                "Fault isolation changed installed packages")
        with (device.output / "uart.raw.log").open("rb") as raw:
            raw.seek(offset)
            fault_log = raw.read()
        expected = (APP_ID + ": JavaScript execution time exceeded" if case.endswith("loop")
                    else APP_ID + ": Error: " + case + " test")
        require(expected.encode("utf-8") in fault_log, "Expected runtime fault was not logged: " + expected)
        (device.output / (case + "-fault-uart.txt")).write_bytes(fault_log)
        entry.update(fault_state=state, expected_error=expected, raw_offset=offset)
        entry["native_home"] = native_fault_home(device, fault_tree)
        device.request("REMOVE", APP_ID, timeout=10)
        require(installed_ids(device) == baseline, "Fault cleanup changed other installed packages")
        after = device.state()
        require(after["registered"] == before["registered"] and after["live"] == before["live"],
                "Faulted instance/registration was not released: " + repr(after))
        entry["healthy_settings"] = native_settings_works(device, case)
        entry.update(status="passed", finished=time.time())
        save()
        print("JS fault isolation passed:", case, flush=True)


def run_suite(device, report, save, verify_reboot=False, faults=False):
    hello = device.request("HELLO", timeout=8, attempts=3)
    require("storage=ready" in hello and "debug=1" in hello,
            "Firmware requires ready BPK storage and debug controls: " + hello)
    report["hello"] = hello
    baseline = installed_ids(device)
    report["initial_ids"] = baseline
    require(APP_ID not in baseline,
            APP_ID + " already exists; this run will neither replace nor remove it")
    report["owns_test_id"] = True
    report["initial_state"] = device.state()
    save()
    packages = build_packages(device.output / "packages", include_faults=faults)
    report["packages"] = {version: dict(bytes=len(data), crc32=f"{bpk.crc(data):08x}",
                                        sha256=hashlib.sha256(data).hexdigest())
                          for version, data in packages.items()}
    # Wake the screen and dismiss an existing quick panel/overview before the
    # first real installer OPEN; do not change settings or close other apps.
    device.request("DBG_HOME")

    def record(stage, **details):
        ids = installed_ids(device)
        require(ids == sorted(baseline + [APP_ID]), "Installed package set changed: " + repr(ids))
        entry = dict(stage=stage, time=time.time(), ids=ids, **details)
        report["stages"].append(entry)
        save()
        print("BPK stage passed:", stage, flush=True)

    require(upload(device, packages["v1"]) == APP_ID, "Install returned an incorrect ID")
    device.request("OPEN", APP_ID, timeout=10)
    state, tree = check_app(device, "v1", 0, device.output, "installed-v1")
    increment(device, tree)
    check_app(device, "v1", 1, device.output, "clicked-v1")
    record("install_open_and_click", state=state)

    # Do not background first: exercise closure of a live JS context on upgrade.
    require(device.state()["active"] == APP_ID, "Upgrade must begin with the app foreground")
    require(upload(device, packages["v2"]) == APP_ID, "Upgrade returned an incorrect ID")
    require(device.state()["active"] == "", "Foreground update did not close the old instance")
    device.request("OPEN", APP_ID, timeout=10)
    state, tree = check_app(device, "v2", 0, device.output, "upgraded-v2")
    increment(device, tree)
    check_app(device, "v2", 1, device.output, "clicked-v2")
    record("foreground_upgrade_and_click", state=state)

    error = expect_rejected(lambda: upload(device, packages["v3"], bpk.crc(packages["v3"]) ^ 1), "END")
    check_app(device, "v2", 1, device.output, "after-bad-crc")
    record("bad_crc_preserves_running_v2", rejection=error)

    error = expect_rejected(lambda: upload(device, packages["bad-schema"]), "END")
    check_app(device, "v2", 1, device.output, "after-bad-schema")
    record("bad_schema_preserves_running_v2", rejection=error)

    data = packages["v3"]
    uploaded = begin_partial_upload(device, data)
    device.request("ABORT")
    state, tree = check_app(device, "v2", 1, device.output, "after-abort")
    increment(device, tree)
    check_app(device, "v2", 2, device.output, "clicked-after-abort")
    record("partial_upload_abort_preserves_v2", state=state, uploaded_bytes=uploaded)

    uploaded = begin_partial_upload(device, data)
    report["timeout_observation"] = dict(status="waiting", uploaded_bytes=uploaded,
                                          threshold_seconds=30, started=time.time())
    save()
    print("Waiting for the firmware's real 30-second upload timeout; no commands sent", flush=True)
    observed = wait_for_upload_timeout(device)
    idle = check_no_upload(device)
    # This also wakes a display that slept during the observation; opening the
    # already-active ID preserves its JS instance and counter value.
    device.request("DBG_OPEN", APP_ID, timeout=10)
    state, tree = check_app(device, "v2", 2, device.output, "after-real-timeout")
    increment(device, tree)
    check_app(device, "v2", 3, device.output, "clicked-after-real-timeout")
    report["timeout_observation"].update(status="observed", **observed)
    record("real_upload_timeout_preserves_v2", state=state, observation=observed, protocol=idle)

    if verify_reboot:
        # Interrupt v3 before END, then use the same reset to verify both the
        # committed v2 persistence and rejection of the incomplete replacement.
        uploaded = begin_partial_upload(device, data)
        report["reboot_observation"] = dict(status="partial_upload_active", pending_version="v3",
                                             uploaded_bytes=uploaded, total_bytes=len(data))
        save()  # no LIST/debug requests are allowed while an upload is active
        reset = reboot_and_restore(device)
        idle = check_no_upload(device)
        require(installed_ids(device) == sorted(baseline + [APP_ID]),
                "Package registry changed after reboot")
        device.request("OPEN", APP_ID, timeout=10)
        state, tree = check_app(device, "v2", 0, device.output, "restored-v2")
        increment(device, tree)
        check_app(device, "v2", 1, device.output, "clicked-restored-v2")
        report["reboot_observation"]["status"] = "committed_v2_restored"
        record("reboot_during_partial_v3_restores_committed_v2", reset=reset, state=state,
               uploaded_bytes=uploaded, protocol=idle)

    require(device.state()["active"] == APP_ID, "Removal must begin with the app foreground")
    device.request("REMOVE", APP_ID, timeout=10)
    require(installed_ids(device) == baseline, "Removal changed other installed packages")
    final = device.state()
    require(final["active"] != APP_ID, "Removed app is still foreground")
    error = expect_rejected(lambda: device.request("OPEN", APP_ID), "OPEN", "ESP_ERR_NOT_FOUND")
    report["stages"].append(dict(stage="foreground_remove", time=time.time(), state=final,
                                 ids=baseline, reopen_rejection=error))
    if faults:
        run_fault_cases(device, report, save, baseline, packages)
        final = device.state()
    report["final_state"] = final
    save()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM10")
    parser.add_argument("--elf")
    parser.add_argument("--output")
    parser.add_argument("--verify-reboot", action="store_true",
                        help="Reset during a partial v3 upload and verify committed v2 restores, with no active upload")
    parser.add_argument("--faults", action="store_true",
                        help="Additionally isolate JS button/onPause throw and infinite-loop faults, then verify native Settings")
    args = parser.parse_args()
    device = None
    report = dict(test="device-bpk", app_id=APP_ID, started=time.time(), status="running",
                  verify_reboot=args.verify_reboot, faults=args.faults, owns_test_id=False, stages=[])
    exit_code = 0

    def save():
        if device is not None:
            path = device.output / "bpk-result.json"
            temp = path.with_suffix(".json.tmp")
            temp.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
            temp.replace(path)

    try:
        device = Device(args.port, args.output, args.elf)
        report["output"] = str(device.output.resolve())
        save()
        run_suite(device, report, save, args.verify_reboot, args.faults)
        report["status"] = "passed"
    except (Exception, KeyboardInterrupt) as error:
        report["status"] = "interrupted" if isinstance(error, KeyboardInterrupt) else "failed"
        report["error"] = dict(type=type(error).__name__, message=str(error), traceback=traceback.format_exc())
        exit_code = 130 if isinstance(error, KeyboardInterrupt) else 1
        print("BPK regression", report["status"] + ":", error, file=sys.stderr, flush=True)
    finally:
        if device is not None:
            if report["owns_test_id"] and healthy(device):
                try:
                    device.request("ABORT", timeout=3, attempts=1)
                    if APP_ID in installed_ids(device):
                        device.request("REMOVE", APP_ID, timeout=10)
                    ids = installed_ids(device)
                    require(ids == report["initial_ids"], "Final installed IDs differ from the original list")
                    report["cleanup"] = dict(passed=True, final_ids=ids)
                except Exception as error:
                    report["cleanup"] = dict(passed=False, error=str(error), test_id=APP_ID)
                    report["status"] = "failed"
                    exit_code = 1
            elif report["owns_test_id"]:
                report["cleanup"] = dict(passed=False, skipped="panic, reboot, or serial failure; preserving evidence",
                                         possible_remaining_test_id=APP_ID)
                report["status"] = "failed"
                exit_code = 1
            save()
            try:
                device.close()
            except Exception as error:
                report["capture_close_error"] = str(error)
                report["status"] = "failed"
                exit_code = 1
            report["panic_seen"] = bool(device.capture.panic)
            report["unexpected_reboot"] = bool(device.rebooted)
            if device.capture.panic or device.rebooted or device.error is not None:
                report["status"] = "failed"
                report["serial_error"] = None if device.error is None else str(device.error)
                exit_code = 1
            report["finished"] = time.time()
            save()
            print("Evidence:", device.output.resolve(), flush=True)
    print(json.dumps(report, ensure_ascii=False, indent=2), flush=True)
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
