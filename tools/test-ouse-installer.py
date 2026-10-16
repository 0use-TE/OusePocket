"""Exercise actual C installer, ZIP parser, LVGL + QuickJS through the PC client.

Host transport replaces UART; files are real. Start with test-ouse-runtime.py.
Packages use the sole four-file BPK layout and an OIC1 BGRA icon.
"""
from pathlib import Path
import importlib.util
import io
import json
import queue
import shutil
import struct
import subprocess
import threading
import time
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / ".tools/ouse-runtime-check"
spec = importlib.util.spec_from_file_location("bpk", ROOT/"tools/bpk.py")
bpk = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bpk)


class HostSerial:
    def __init__(self):
        self.process = subprocess.Popen([str(OUT/"check.exe"), "usb-server"], cwd=OUT,
                                        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.queue = queue.Queue()
        self.buffer = bytearray()
        self.drop = False
        def reader():
            for line in iter(self.process.stdout.readline, b""):
                if self.drop and b"OPK1 " in line:
                    self.drop = False
                    continue
                self.queue.put(line)
        self.reader = threading.Thread(target=reader, daemon=True)
        self.reader.start()
        def errors():
            for line in iter(self.process.stderr.readline,b""):
                sys.stderr.write(line.decode("utf-8","replace"))
        threading.Thread(target=errors,daemon=True).start()
        deadline = time.monotonic()+5
        while time.monotonic() < deadline:
            if b"HOST READY" in self.read(1024):
                return
        raise AssertionError("Host did not start")

    def write(self, data):
        # Deliberately fragment every request including header/CRC boundaries.
        for offset in range(0, len(data), 7):
            self.process.stdin.write(data[offset:offset+7])
        self.process.stdin.flush()

    def flush(self):
        pass

    def read(self, n):
        if not self.buffer:
            try:
                self.buffer.extend(self.queue.get(timeout=.01))
            except queue.Empty:
                return b""
        chunk = bytes(self.buffer[:n])
        del self.buffer[:n]
        return chunk

    def close(self):
        self.process.stdin.close()
        assert self.process.wait(timeout=5) == 0, self.process.stderr.read().decode("utf-8", "replace")
        self.reader.join(timeout=1)


def client(token=0x12345678):
    c = bpk.Client.__new__(bpk.Client)
    c.serial = HostSerial()
    c.token = token
    c.sequence = 0
    c.pending = bytearray()
    assert "storage=ready" in c.request("HELLO", timeout=.2)
    return c


def package(app_id="user.counter", script=None, page=None, manifest=None, names=None, compression=0, icon=None):
    source = ROOT/"examples/counter"
    m = json.loads((source/"manifest.json").read_text(encoding="utf-8"))
    m["id"] = app_id
    if manifest:
        m.update(manifest)
    contents = [json.dumps(m, ensure_ascii=False).encode("utf-8"),
                page if page is not None else (source/"page.json").read_bytes(),
                script if script is not None else (source/"app.js").read_bytes(),
                icon if icon is not None else (source/"icon.bin").read_bytes()]
    output = io.BytesIO()
    with zipfile.ZipFile(output, "w", compression=compression) as z:
        for name, content in zip(names or ["manifest.json", "page.json", "app.js", "icon.bin"], contents):
            z.writestr(name, content)
    return output.getvalue()


def transfer(c, data):
    c.request("BEGIN", f"{len(data):08x}:{bpk.crc(data):08x}", timeout=.2)
    for offset in range(0, len(data), 512):
        c.request("DATA", f"{offset:08x}:{data[offset:offset+512].hex()}", timeout=.2)
    return c.request("END", timeout=2)


def rejected(call):
    try:
        call()
    except RuntimeError:
        return
    raise AssertionError("Expected ERR response")


storage = OUT/"usb-storage"
if storage.exists():
    assert storage.resolve().is_relative_to(OUT.resolve())
    shutil.rmtree(storage)
c = client()
assert c.request("LIST", timeout=.2) == "-"
rejected(lambda: c.request("BEGIN", "00011171:00000000", timeout=.2))  # >70000

# Bad CRC, unrelated console data, binary bytes, oversized line: recover at LF.
c.serial.write(b"console noise\n" + bpk.wire(c.token, 55, "HELLO", "-")[:-4] + b"000\n")
c.serial.write(b"x"*1400+b"\n\x00binary\n")
assert "core=" in c.request("HELLO", timeout=.2)

data = package()
assert c.install(data) == "user.counter"
saved = storage/"ouse-apps/user.counter.bpk"
assert saved.read_bytes() == data
assert c.request("LIST", timeout=.2) == "user.counter"
assert c.request("OPEN", "user.counter", timeout=.2) == "-"  # genuine JS UI

# Losing the ACK must not append DATA twice, repeat END, or fail REMOVE retry.
update = package(script=b'Ouse.ui.setText("value","updated");')
c.request("BEGIN", f"{len(update):08x}:{bpk.crc(update):08x}", timeout=.2)
for offset in range(0, len(update), 512):
    c.serial.drop = True
    assert int(c.request("DATA", f"{offset:08x}:{update[offset:offset+512].hex()}", timeout=.1)) == min(offset+512,len(update))
c.serial.drop = True
assert c.request("END", timeout=.1) == "user.counter"
assert saved.read_bytes() == update
assert c.request("OPEN", "user.counter", timeout=.2) == "-"

# Deleting the persisted file can succeed before dispatching the UI removal
# fails. Retrying must close the running app and reclaim the registry slot.
retry_id = "user.remove-retry"
retry_data = package(app_id=retry_id)
retry_saved = storage/f"ouse-apps/{retry_id}.bpk"
assert transfer(c, retry_data) == retry_id
assert c.request("OPEN", retry_id, timeout=.2) == "-"
c.serial.write(b"\x05")  # a genuine filesystem error must preserve both records
rejected(lambda: c.request("REMOVE", retry_id, timeout=.2))
assert retry_saved.read_bytes() == retry_data
assert retry_id in c.request("LIST", timeout=.2).split(",")
assert c.request("OPEN", retry_id, timeout=.2) == "-"
c.serial.write(b"\x04")  # file deletion succeeds, then actual core UI lock fails
rejected(lambda: c.request("REMOVE", retry_id, timeout=.2))
assert not retry_saved.exists()
assert retry_id in c.request("LIST", timeout=.2).split(",")
assert c.request("REMOVE", retry_id, timeout=.2) == "-"
assert retry_id not in c.request("LIST", timeout=.2).split(",")
rejected(lambda: c.request("OPEN", retry_id, timeout=.2))
rejected(lambda: c.request("REMOVE", retry_id, timeout=.2))  # absent registry is not success
assert transfer(c, retry_data) == retry_id  # the same ID/slot can be installed again
assert c.request("OPEN", retry_id, timeout=.2) == "-"
assert c.request("REMOVE", retry_id, timeout=.2) == "-"

# Corrupt/traversal/duplicate/compressed/foreign-runtime/schema/syntax packages
# must preserve both disk package and running registration.
bad_crc = bytearray(data)
bad_crc[50] ^= 1
invalid = [bytes(bad_crc), data+b"trailing", data[:-1],
           package(names=["../manifest.json","page.json","app.js","icon.bin"]),
           package(names=["manifest.json","page.json","page.json","icon.bin"]),
           package(compression=zipfile.ZIP_DEFLATED),
           package(manifest={"runtime":"brookesia"}), package(app_id="ouse.settings"),
           package(manifest={"names":{"fr":"Counter"}}),
           package(manifest={"names":{"zh_CN":""}}),
           package(manifest={"icon":"icon.png"}),
           package(icon=b"OIC1" + struct.pack("<HH", 97, 1) + b"\xff" * (97 * 4)),
           package(icon=b"OIC1" + struct.pack("<HH", 1, 1) + b"\xff" * 3),
           package(app_id="user../bad"), package(script=b"const broken = ;"),
           package(page=b"{"), package(page=b'{"api":1,"units":"dp","root":{"type":"alien","id":"x"}}')]
for item in invalid:
    rejected(lambda item=item: transfer(c,item))
    assert saved.read_bytes() == update
    assert c.request("LIST", timeout=.2) == "user.counter"

# Flash sync/rename failure must preserve the old committed package.
for failure in (b"\x02",b"\x03"):
    c.serial.write(failure)
    rejected(lambda: transfer(c,data))
    assert saved.read_bytes()==update

# Compiler-only validation never runs uploaded code during installation.
assert transfer(c, package(app_id="user.never-run",script=b"while(true){}")) == "user.never-run"
assert c.request("OPEN", "user.never-run", timeout=2) == "-"  # runtime execution budget catches loop
assert transfer(c, package(app_id="user.third")) == "user.third"
rejected(lambda: transfer(c, package(app_id="user.full")))  # host fixture: settings + 4 native test registrations + 3 installed
assert not (storage/"ouse-apps/user.full.bpk").exists()
rejected(lambda: c.request("REMOVE", "ouse.settings", timeout=.2))

# Disconnect during upload: only old committed package survives reboot.
c.request("BEGIN", f"{len(data):08x}:{bpk.crc(data):08x}", timeout=.2)
c.request("DATA", "00000000:"+data[:512].hex(), timeout=.2)
c.close()
c = client(0x23456789)
assert set(c.request("LIST", timeout=.2).split(",")) == {"user.counter","user.never-run","user.third"}
assert c.request("OPEN", "user.counter", timeout=.2) == "-"
assert saved.read_bytes() == update

# Partial upload timeout releases ownership; no polling loop in idle service.
c.request("BEGIN", f"{len(data):08x}:{bpk.crc(data):08x}", timeout=.2)
c.serial.write(b"\x01")  # host fixture advances session idle time >30 seconds
assert c.request("BEGIN", f"{len(data):08x}:{bpk.crc(data):08x}", timeout=.2) == "-"
c.request("ABORT", timeout=.2)
for app_id in ["user.never-run","user.third","user.counter"]:
    c.serial.drop = True
    assert c.request("REMOVE",app_id,timeout=.1) == "-"
assert c.request("LIST", timeout=.2) == "-"
c.close()
c = client(0x34567890)
assert c.request("LIST", timeout=.2) == "-"
c.close()
print("PASS actual C installer + PC client: fragmented frames, CRC, ACK retry, bounded ZIP, JS validation, atomic update, capacity, timeout, reboot restore, uninstall/UI-lock failure retry, filesystem error preservation, JS/LVGL cleanup")
