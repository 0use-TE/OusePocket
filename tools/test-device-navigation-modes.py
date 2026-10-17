"""Exercise native navigation through the device debug transport."""
import argparse
import json
from pathlib import Path
import time

import bpk


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--stage", choices=["all", "buttons", "restore"], default="all")
    parser.add_argument("--output", type=Path, default=Path(".tools/navigation-check"))
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

    def tap(horizontal, vertical):
        command("DBG_TOUCH", f"{horizontal}:{vertical}:1")
        time.sleep(0.06)
        command("DBG_TOUCH", f"{horizontal}:{vertical}:0")
        time.sleep(0.35)

    def text(label):
        node = next(node for node in tree() if node["text"] == label and 40 <= node["bounds"][1] and node["bounds"][3] < 440)
        left, top, right, bottom = node["bounds"]
        tap((left + right) // 2, (top + bottom) // 2)

    def appearance():
        command("DBG_OPEN", "ouse.settings")
        text("外观")

    def key(index):
        current = state()
        tap(round(current["width"] * (index + 0.5) / 3), current["height"] - 20)

    def swipe(start, end):
        for step in range(9):
            horizontal = round(start[0] + (end[0] - start[0]) * step / 8)
            vertical = round(start[1] + (end[1] - start[1]) * step / 8)
            command("DBG_TOUCH", f"{horizontal}:{vertical}:1")
            time.sleep(0.018)
        command("DBG_TOUCH", f"{end[0]}:{end[1]}:0")
        time.sleep(0.4)

    try:
        print(command("HELLO"), flush=True)
        original = state()
        assert original["width"] == 320 and original["height"] == 480
        if args.stage in ("buttons", "all"):
            appearance()
            text("三键导航")
            assert state()["navigation"] == 1
            assert any(node["depth"] == 1 and node["bounds"] == [0, 40, 319, 439] for node in tree())
            shot("appearance-buttons")
            text("深色模式")
            shot("appearance-buttons-dark")
            text("深色模式")
            swipe((0, 240), (95, 240))
            assert any(node["text"] == "三键导航" for node in tree())
            swipe((160, 474), (160, 390))
            assert state()["active"] == "ouse.settings" and not state()["recents"]
            swipe((300, 8), (300, 290))
            assert state()["quick"]
            key(0)
            assert not state()["quick"]
            key(0)
            assert any(node["text"] == "设置" for node in tree())
            assert not any(node["text"] == "三键导航" for node in tree())
            key(1)
            assert not state()["active"]
            key(2)
            assert state()["recents"] and state()["cards"] > 0
            shot("recents-buttons")
            key(0)
            assert not state()["recents"]
            key(2)
            key(1)
            assert not state()["recents"] and not state()["active"]
            time.sleep(0.6)
            print("PASS three-button switching, settings back, home and overview", flush=True)
        if args.stage in ("restore", "all"):
            assert state()["navigation"] == 1
            print("PASS persisted three-button mode present", flush=True)
            appearance()
            text("全面屏手势")
            assert state()["navigation"] == 0
            shot("appearance-gestures")
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
        assert command("DBG_CHECK") == "ok=1"
        print("PASS heap integrity", json.dumps(state()), flush=True)
    finally:
        client.close()


if __name__ == "__main__":
    main()
