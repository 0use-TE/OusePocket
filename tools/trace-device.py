"""Passively preserve UART bytes and complete ESP-IDF core dumps; never reset/flash.

Use the matching ELF, keep other serial readers closed, then reproduce on screen.
"""
from pathlib import Path
import argparse
import base64
import hashlib
import json
import re
import shutil
import struct
import time
from datetime import datetime, timezone, timedelta

START = b"================= CORE DUMP START ================="
END = b"================= CORE DUMP END ================="


class Capture:
    def __init__(self, directory, elf_sha, display=print):
        self.directory = Path(directory)
        self.directory.mkdir(parents=True, exist_ok=True)
        self.elf_sha = elf_sha
        self.display = display
        self.raw = (self.directory/"uart.raw.log").open("wb", buffering=0)
        self.pending = bytearray()
        self.dump = None
        self.dumps = []
        self.bytes = 0
        self.panic = False
        self.reported_sha = None
        self.complete = False
        self.closed = False
        self.dump_started = self.last_progress = None
        self.save()

    def save(self):
        data = dict(elf_sha256=self.elf_sha, reported_elf_prefix=self.reported_sha,
                    elf_matches=None if self.reported_sha is None else self.elf_sha.startswith(self.reported_sha),
                    raw_bytes=self.bytes, panic_seen=self.panic, dumps=self.dumps,
                    closed=self.closed)
        temp = self.directory/"capture.json.tmp"
        temp.write_text(json.dumps(data, indent=2), encoding="utf-8")
        temp.replace(self.directory/"capture.json")

    def feed(self, data):
        self.raw.write(data)  # preserve everything BEFORE parsing/printing
        self.bytes += len(data)
        self.pending.extend(data)
        while b"\n" in self.pending:
            line, _, tail = self.pending.partition(b"\n")
            self.pending = bytearray(tail)
            self.line(bytes(line).rstrip(b"\r"))
        if len(self.pending) > 65536:
            self.display("Oversized unterminated line; raw data preserved")
            self.pending.clear()

    def line(self, line):
        if b"Guru Meditation" in line or b"panic'ed" in line:
            self.panic = True
            self.save()
        match = re.search(rb"ELF file SHA256:\s*([a-fA-F0-9]+)", line)
        if match:
            self.reported_sha = match[1].decode("ascii").lower()
            self.save()
        if START in line:
            if self.dump:
                self.dump.close()  # preserve previous incomplete dump separately
            info = dict(file=f"core-{len(self.dumps)+1}.b64", complete=False,
                        binary_bytes=0, expected_bytes=None, invalid_lines=0)
            self.dumps.append(info)
            self.complete = False  # completion refers to the latest dump
            self.dump = (self.directory/info["file"]).open("wb", buffering=0)
            self.dump_started = self.last_progress = time.monotonic()
            self.display("CORE DUMP START: saving all Base64 to disk")
            self.save()
            return
        if END in line and self.dump:
            self.dump.close()
            self.dump = None
            info = self.dumps[-1]
            info["complete"] = True
            info["length_matches"] = info["expected_bytes"] == info["binary_bytes"]
            self.complete = True
            self.display(f"CORE DUMP END: {info['binary_bytes']} binary bytes; length matches={info['length_matches']}")
            self.save()
            return
        if self.dump:
            info = self.dumps[-1]
            try:
                payload = base64.b64decode(line, validate=True)
                if not line or not payload:
                    raise ValueError("empty line")
            except (ValueError, base64.binascii.Error):
                info["invalid_lines"] += 1
                self.display("Non-Base64 line inside dump (preserved in raw log): " + line.decode("utf-8", "replace"))
                return
            # IDF encodes individual chunks, possibly with padding between chunks.
            # Preserve line boundaries; concatenating then decoding can lose data.
            self.dump.write(line+b"\n")
            if info["binary_bytes"] == 0 and len(payload) >= 4:
                info["expected_bytes"] = struct.unpack_from("<I", payload)[0]
            info["binary_bytes"] += len(payload)
            now = time.monotonic()
            if now-self.last_progress >= 5:
                total = info["expected_bytes"]
                n = info["binary_bytes"]
                self.display(f"Dump received {n}/{total} binary bytes" + (f" ({100*n/total:.1f}%)" if total else ""))
                self.last_progress = now
                self.save()
            return
        if line:
            self.display(line.decode("utf-8", "replace"))

    def close(self):
        if self.dump:
            self.dump.close()
            self.dump = None
        self.raw.close()
        self.closed = True
        self.save()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM10")
    parser.add_argument("--baud", type=int, default=921600)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--seconds", type=float, default=360)
    parser.add_argument("--dump-timeout", type=float, default=1200)
    args = parser.parse_args()
    if not args.elf.is_file():
        parser.error("Matching ELF does not exist")
    stamp = datetime.now(timezone(timedelta(hours=8))).strftime("%Y%m%d-%H%M%S")
    output = args.output or Path(".tools/diagnostics")/("trace-"+stamp)
    output.mkdir(parents=True, exist_ok=True)
    if (output/"uart.raw.log").exists():
        parser.error("Output already contains a trace; use a new directory")
    elf_sha = hashlib.sha256(args.elf.read_bytes()).hexdigest()
    for source in (args.elf, args.elf.with_suffix(".map"), args.elf.parent/"sdkconfig.h"):
        if source.is_file():
            shutil.copy2(source, output/source.name)
    import serial
    port = serial.Serial(port=None, baudrate=args.baud, timeout=.25, rtscts=False, dsrdtr=False)
    port.dtr = False
    port.rts = False
    port.port = args.port
    port.open()
    capture = Capture(output, elf_sha, lambda text: print(text, flush=True))
    started = last_data = time.monotonic()
    finished = None
    print(f"TRACE READY: {args.port} {args.baud}; {output.resolve()}; ELF {elf_sha}", flush=True)
    try:
        while True:
            chunk = port.read(max(1, min(port.in_waiting, 16384)))
            now = time.monotonic()
            if chunk:
                last_data = now
                capture.feed(chunk)
            if capture.complete:
                if finished is None:
                    finished = now
                if now-finished > 6:
                    break
            elif capture.dump_started is not None:
                if now-last_data > 30 or now-capture.dump_started > args.dump_timeout:
                    print("Dump stalled/timed out; incomplete data preserved", flush=True)
                    break
            elif now-started > args.seconds:
                print("Observation interval ended; raw log preserved", flush=True)
                break
    except KeyboardInterrupt:
        print("Capture stopped; data preserved", flush=True)
    finally:
        port.close()
        capture.close()
    print(f"TRACE SAVED: {output.resolve()}", flush=True)


if __name__ == "__main__":
    main()
