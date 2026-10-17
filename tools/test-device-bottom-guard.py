"""Check bottom-edge ownership, rounded package icons and read-only SD detection."""
import argparse
import json
from pathlib import Path
import time

import bpk


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--output", type=Path, default=Path(".tools/bottom-guard-check"))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    client = bpk.Client(args.port, 921600)
    client.serial.timeout = 0.02

    def command(verb, payload="-", timeout=10):
        return client.request(verb, payload, timeout=timeout, attempts=1)

    def state():
        return json.loads(bytes.fromhex(command("DBG_STATE")))

    def resource(verb):
        size, checksum = command(verb).split(":")
        data = bytearray()
        try:
            for offset in range(0, int(size), 256):
                data.extend(bytes.fromhex(command("DBG_READ", f"{offset}:{min(256, int(size)-offset)}")))
        finally:
            command("DBG_FREE")
        assert bpk.crc(data) == int(checksum, 16)
        return data

    def tree():
        return json.loads(resource("DBG_TREE"))["nodes"]

    def shot(name):
        (args.output / (name + ".jpg")).write_bytes(resource("DBG_SHOT"))

    def swipe(start, end):
        for step in range(9):
            horizontal = round(start[0] + (end[0] - start[0]) * step / 8)
            vertical = round(start[1] + (end[1] - start[1]) * step / 8)
            command("DBG_TOUCH", f"{horizontal}:{vertical}:1")
            time.sleep(0.015)
        command("DBG_TOUCH", f"{end[0]}:{end[1]}:0")
        time.sleep(0.35)

    def scroll_position():
        node = next(node for node in tree() if node["type"] == "object" and node["depth"] == 3 and node["bounds"] == [0, 40, 319, 455])
        return node["scroll_y"]

    def text(label):
        for attempt in range(9):
            node = next((node for node in tree() if node["text"] == label and 40 <= node["bounds"][1] and node["bounds"][3] < 450), None)
            if node:
                left, top, right, bottom = node["bounds"]
                horizontal, vertical = (left + right) // 2, (top + bottom) // 2
                command("DBG_TOUCH", f"{horizontal}:{vertical}:1")
                time.sleep(0.05)
                command("DBG_TOUCH", f"{horizontal}:{vertical}:0")
                time.sleep(0.4)
                return
            swipe((180, 370), (180, 200))
        raise AssertionError("Unreachable: " + label)

    try:
        print(command("HELLO"), flush=True)
        print("SD", command("DBG_SD"), flush=True)
        current = state()
        assert current["width"] == 320 and current["height"] == 480 and current["navigation"] == 0
        command("DBG_HOME")
        if "ouse.settings" in current["alive"]:
            command("DBG_CLOSE", "ouse.settings")
        command("DBG_OPEN", "ouse.settings")
        before = scroll_position()
        swipe((160, 450), (160, 430))
        assert state()["active"] == "ouse.settings" and scroll_position() == before
        swipe((160, 360), (160, 220))
        scrolled = scroll_position()
        assert scrolled > before
        swipe((160, 450), (160, 390))
        assert not state()["active"]
        command("DBG_OPEN", "ouse.settings")
        assert scroll_position() == scrolled
        print("PASS system edge owns touch; content scrolls only when started inside app", flush=True)
        text("系统信息")
        for attempt in range(7):
            if any(node["text"] == "SD 卡" and 40 <= node["bounds"][1] < 370 for node in tree()):
                shot("sd-status")
                break
            swipe((180, 370), (180, 200))
        else:
            raise AssertionError("SD status not reachable")
        command("DBG_HOME")
        nodes = tree()
        frames = [node for node in nodes if node["type"] == "object" and node["bounds"][2]-node["bounds"][0] == 39 and node["radius"] == 10]
        assert len(frames) >= 2
        shot("desktop-rounded")
        print("PASS rounded package icon frames", len(frames), flush=True)
        captured = bytearray()
        original_read = client.serial.read
        def capture(size=1):
            data = original_read(size)
            captured.extend(data)
            return data
        client.serial.read = capture
        try:
            native = command("DBG_TEST", timeout=40)
        finally:
            client.serial.read = original_read
            (args.output / "native.log").write_bytes(captured)
        assert "failures=0" in native, native
        print("PASS gesture regression", native, flush=True)
        command("DBG_HOME")
        command("DBG_CLOSE", "ouse.settings")
        print("SD", command("DBG_SD"), flush=True)
        print("HEAP", command("DBG_HEAP"), flush=True)
        assert command("DBG_CHECK") == "ok=1"
        print("PASS heap integrity", flush=True)
    finally:
        client.close()


if __name__ == "__main__":
    main()
