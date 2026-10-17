"""Exercise the installed SD explorer through the native debug transport."""
import argparse
import json
from pathlib import Path
import time
import bpk


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--package", type=Path)
    parser.add_argument("--music-package", type=Path)
    parser.add_argument("--scroll-test", action="store_true")
    parser.add_argument("--output", type=Path, default=Path("explorer-check"))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    client = bpk.Client(args.port, 921600)
    client.serial.timeout = 0.005
    report = {};temporary=[]

    def command(verb, payload="-", timeout=15):
        return client.request(verb, payload, timeout=timeout, attempts=3)

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

    def wait_label(label):
        for attempt in range(8):
            nodes = tree()
            if any(node["text"] == label for node in nodes):
                return nodes
            time.sleep(0.4)
        raise AssertionError("Missing label: " + label + " " + repr(nodes))

    def tap(label):
        nodes = wait_label(label)
        node = next(node for node in nodes if node["text"] == label and 40 <= node["bounds"][1] and node["bounds"][3] < 440)
        left, top, right, bottom = node["bounds"]
        horizontal, vertical = (left+right)//2, (top+bottom)//2
        command("DBG_TOUCH", f"{horizontal}:{vertical}:1")
        time.sleep(0.07)
        command("DBG_TOUCH", f"{horizontal}:{vertical}:0")
        time.sleep(0.5)

    def shot(name):
        (args.output / (name+".jpg")).write_bytes(resource("DBG_SHOT"))

    def swipe(start, end):
        for step in range(9):
            vertical=round(start+(end-start)*step/8)
            command("DBG_TOUCH",f"160:{vertical}:1")
            time.sleep(0.03)
        command("DBG_TOUCH",f"160:{end}:0")
        time.sleep(0.6)

    def side_back():
        for step in range(9):
            command("DBG_TOUCH",f"{round(step*110/8)}:240:1")
            time.sleep(0.025)
        command("DBG_TOUCH","110:240:0");time.sleep(0.7)

    def seek(label, start, end):
        for attempt in range(24):
            nodes=tree()
            if any(node["text"]==label and 105<=node["bounds"][1] and node["bounds"][3]<435 for node in nodes):return nodes
            swipe(start,end)
        raise AssertionError("Scroll could not find "+label)

    try:
        report["hello"] = command("HELLO")
        assert "core=0.18.2" in report["hello"]
        report["sd"] = command("DBG_SD")
        assert "ready=1" in report["sd"]
        if args.package:
            client.install(args.package.read_bytes())
        if args.music_package:
            client.install(args.music_package.read_bytes())
            command("DBG_OPEN","user.music");time.sleep(1)
            report["music_runtime"]=command("DBG_RUNTIME");assert "errors=0" in report["music_runtime"]
            shot("music-updated");command("DBG_CLOSE","user.music")
        command("DBG_OPEN", "user.explorer")
        report["root"] = wait_label("Ouse")
        assert not any(node["text"].startswith(("[目录]","[文件]","本页","第 1 页","SD 卡 /")) for node in report["root"])
        shot("root")
        tap("Ouse")
        report["ouse"] = wait_label("Music")
        tap("Music")
        report["music"] = wait_label("fur-elise-30s.wav")
        shot("music-folder")
        tap("fur-elise-30s.wav")
        report["details"] = wait_label("文件详情")
        assert any("468.8 KiB" in node["text"] for node in report["details"])
        shot("file-details")
        side_back()
        wait_label("fur-elise-30s.wav")
        assert not any(node["text"]=="文件详情" for node in tree())
        side_back()
        wait_label("Music")
        assert json.loads(bytes.fromhex(command("DBG_STATE")))["active"]=="user.explorer"
        if args.scroll_test:
            nonce=f"{int(time.time())%65536:04x}"
            names=[f"scroll-{nonce}-{index:02d}.bin.part" for index in range(32)]
            for name in names:
                target="Downloads/"+name
                temporary.append(target[:-5])
                command("DBG_FBEGIN",target+f":1:{bpk.crc(b'x'):08x}")
                command("DBG_FDATA","00000000:78");command("DBG_FEND")
            tap("Downloads");wait_label(names[0])
            report["scroll_bottom"]=seek(names[-1],385,160)
            shot("continuous-bottom")
            report["scroll_top"]=seek(names[0],150,385)
            shot("continuous-top")
            assert not any(node["text"] in ("上一页","下一页") for node in tree())
            tap("返回");wait_label("Music")
        side_back()
        wait_label("Ouse")
        assert any(node["text"]=="SD 卡" for node in tree())
        side_back()
        assert json.loads(bytes.fromhex(command("DBG_STATE")))["active"]==""
        command("DBG_OPEN","user.explorer");wait_label("Ouse")
        report["side_back"]="dialog, Music to Ouse, Ouse to SD root, root to home"
        tap("刷新")
        wait_label("Ouse")
        tap("信息");wait_label("存储信息");tap("知道了")
        command("DBG_HOME")
        command("DBG_OPEN", "user.explorer")
        wait_label("Ouse")
        command("DBG_CLOSE", "user.explorer")
        command("DBG_OPEN", "user.explorer")
        wait_label("Ouse")
        report["runtime"] = command("DBG_RUNTIME")
        assert "errors=0" in report["runtime"]
        report["heap"] = command("DBG_HEAP")
        assert command("DBG_CHECK") == "ok=1"
        report["passed"] = True
        print("PASS installed explorer: SD root, nested folders, file details, refresh, resume, close/reopen, runtime and heap", flush=True)
        print(report["sd"], flush=True)
        print(report["runtime"], flush=True)
        print(report["heap"], flush=True)
    finally:
        for target in temporary:
            try:command("DBG_FABORT",target)
            except Exception as error:print("Cleanup failed",target,error,flush=True)
        client.close()
        (args.output / "result.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
