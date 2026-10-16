"""Decode a completed trace offline, verify SHA256, and report both core tasks."""
from pathlib import Path
import argparse
import hashlib
import json
from contextlib import redirect_stdout, redirect_stderr


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--gdb", required=True)
    parser.add_argument("--dump",type=int,help="One-based dump index; default selects the latest complete dump")
    args = parser.parse_args()
    directory = args.directory.resolve()
    meta = json.loads((directory/"capture.json").read_text(encoding="utf-8"))
    elf = directory/"ouse_core_desktop.elf"
    sha = hashlib.sha256(elf.read_bytes()).hexdigest()
    if sha != meta["elf_sha256"] or not meta.get("reported_elf_prefix") or not sha.startswith(meta["reported_elf_prefix"]):
        raise SystemExit("Refusing to decode: missing/mismatching panic ELF fingerprint")
    completed = [dump for dump in meta["dumps"] if dump["complete"] and dump.get("length_matches")]
    if not completed:
        raise SystemExit("No complete dump with matching declared length; raw bytes are preserved")
    from esp_coredump import CoreDump

    class DeviceDump(CoreDump):
        def print_all_memory_regions(self):
            # Full segment listings can be enormous. Inspect the kernel and
            # current UI/driver state instead; the complete ELF remains saved.
            print("\n================ PRODUCT / BOTH CORE STATE ================")
            commands = [
                "p/x pxCurrentTCBs", "p pxCurrentTCBs[0]->pcTaskName", "p pxCurrentTCBs[1]->pcTaskName",
                "p/x xIsrStackTop", "p/x xIsrStackBottom", "p/x xKernelLock",
                "p port_uxCriticalNesting", "p uxSchedulerSuspended", "p xYieldPending",
                "p/x s_ouse_fault[0]", "p/x s_ouse_fault[1]", "p 'ouse_core.c'::core.settings_page",
                "p 'ouse_apps.c'::apps.active", "p 'ouse_screen.c'::screen.asleep",
                "p 'board_wifi.c'::state.ready", "p 'board_wifi.c'::state.scanning",
                "p 'board_wifi.c'::state.error", "p 'ouse_runtime.c'::instances",
            ]
            for command in commands:
                print("\nGDB:", command)
                try:
                    print(self.gdb_esp.run_cmd(command))
                except Exception as error:
                    print("Unavailable:", type(error).__name__, str(error))

    selected=completed[-1]
    if args.dump is not None:
        if not 1<=args.dump<=len(meta["dumps"]):
            raise SystemExit("Dump index is out of range")
        selected=meta["dumps"][args.dump-1]
        if selected not in completed:
            raise SystemExit("Selected dump is incomplete or its length does not match")
    core_path = directory/selected["file"]
    report = directory/("decoded.txt" if args.dump is None else f"decoded-{args.dump}.txt")
    with report.open("w", encoding="utf-8") as output, redirect_stdout(output), redirect_stderr(output):
        decoder = DeviceDump(chip="esp32p4", core=str(core_path), core_format="b64", prog=str(elf),
                             gdb=args.gdb, gdb_timeout_sec=10,
                             save_core=str(directory/("core.elf" if args.dump is None else f"core-{args.dump}.elf")))
        decoder.info_corefile()  # official loader validates the dump SHA256
    print("Decoded and checksum-validated:", report)


if __name__ == "__main__":
    main()
