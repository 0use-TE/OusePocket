"""Copy files to the device's scoped SD folders without overwriting existing data."""
import argparse
from pathlib import Path
import bpk


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--folder", choices=["Music", "Downloads", "Video", "Pictures"], default="Music")
    parser.add_argument("--discard-partial", action="store_true", help="Explicitly discard stale .part files for missing destination files")
    args = parser.parse_args()
    files = [args.source] if args.source.is_file() else sorted(file for file in args.source.iterdir() if file.suffix == ".wav" or file.name == "LICENSES.txt")
    client = bpk.Client(args.port, 921600)
    client.serial.timeout = 0.001
    try:
        print(client.request("HELLO"), flush=True)
        print(client.request("DBG_SD"), flush=True)
        for file in files:
            data = file.read_bytes()
            target = args.folder + "/" + file.name
            expected = f"{len(data)}:{bpk.crc(data):08x}"
            try:
                existing = client.request("DBG_FINFO", target, timeout=30)
            except RuntimeError as error:
                if "ESP_ERR_NOT_FOUND" not in str(error):
                    raise
            else:
                if existing != expected:
                    raise ValueError("Refusing to overwrite different existing file: " + target)
                print("Verified existing", target, flush=True)
                continue
            if args.discard_partial:
                client.request("DBG_FABORT", target)
            client.request("DBG_FBEGIN", target + ":" + expected)
            try:
                for offset in range(0, len(data), 256):
                    chunk = data[offset:offset+256]
                    reply = client.request("DBG_FDATA", f"{offset:08x}:"+chunk.hex(), timeout=1, attempts=10)
                    if int(reply) != offset + len(chunk):
                        raise RuntimeError("Unexpected SD upload offset")
                    if offset % (256*1024) == 0:
                        print(target, offset, "/", len(data), flush=True)
                client.request("DBG_FEND", timeout=30)
            except BaseException:
                client.request("DBG_FABORT")
                raise
            actual = client.request("DBG_FINFO", target, timeout=30)
            if actual != expected:
                raise RuntimeError("SD readback checksum mismatch: " + target)
            print("Verified SD readback", target, expected, flush=True)
    finally:
        client.close()


if __name__ == "__main__":
    main()
