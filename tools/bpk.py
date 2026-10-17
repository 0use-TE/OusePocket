"""Build and install Ouse Core BPKs over the board's USB/UART bridge.

Protocol OPK1 is independent of the archived Brookesia USB service.
Packing PNG icons additionally requires Pillow; preconverted icon.bin packages
use only the stdlib. Serial commands require pyserial 3.5.
"""
from pathlib import Path
import argparse
import binascii
import json
import re
import secrets
import struct
import sys
import time
import zipfile

MAX_PACKAGE = 524288
MAX_MANIFEST = 8192
MAX_ICON_SIDE = 96
MAX_ICON_PIXELS = 36864
MAX_ASSETS = 16
MAX_IMAGE_SIDE = 512
MAX_IMAGE_BYTES = 128 * 1024
MAX_DATA_BYTES = 128 * 1024
FILES = ("manifest.json", "page.json", "app.js", "icon.bin")


def crc(data):
    return binascii.crc32(data) & 0xffffffff


def _asset_path(value):
    return (isinstance(value, str) and len(value) <= 95 and
            re.fullmatch(r"assets/(?:[A-Za-z0-9._-]+/)*[A-Za-z0-9._-]+", value) is not None and
            all(part not in (".", "..") for part in value.split("/")))


def _assets(manifest):
    assets = manifest.get("assets", [])
    if not isinstance(assets, list) or len(assets) > MAX_ASSETS:
        raise ValueError("assets must be an array with at most 16 entries")
    ids, paths = set(), set()
    for asset in assets:
        if not isinstance(asset, dict) or set(asset) != {"id", "type", "path"}:
            raise ValueError("Each asset requires exactly id, type and path")
        asset_id, kind, name = asset["id"], asset["type"], asset["path"]
        if not isinstance(asset_id, str) or re.fullmatch(r"[A-Za-z][A-Za-z0-9_-]{0,30}", asset_id) is None or asset_id in ids:
            raise ValueError("Asset IDs must be unique ASCII identifiers of 1..31 characters")
        if kind not in ("image", "text", "binary"):
            raise ValueError("Asset type must be image, text or binary")
        if not _asset_path(name) or name in paths:
            raise ValueError("Asset paths must be unique canonical ASCII paths under assets/, at most 95 bytes")
        ids.add(asset_id)
        paths.add(name)
    return assets


def _unzip(data):
    """Match the board's narrow stored ZIP profile, including every offset."""
    if not 22 <= len(data) <= MAX_PACKAGE:
        raise ValueError("BPK must be a ZIP of at most 524288 bytes")
    def u16(offset):
        return struct.unpack_from("<H", data, offset)[0]
    def u32(offset):
        return struct.unpack_from("<I", data, offset)[0]
    end = len(data) - 22
    count = u16(end + 10)
    if (u32(end) != 0x06054b50 or u16(end + 4) or u16(end + 6) or
            not 4 <= count <= 4 + MAX_ASSETS or u16(end + 8) != count or u16(end + 20)):
        raise ValueError("BPK requires four fixed files and at most 16 declared assets, without comments")
    central, central_size = u32(end + 16), u32(end + 12)
    if central > end or central_size != end - central:
        raise ValueError("Invalid ZIP central directory range")
    content, locals_ = {}, {}
    pos = 0
    for _ in range(count):
        if central - pos < 30 or u32(pos) != 0x04034b50:
            raise ValueError("Invalid ZIP local header")
        size, length = u32(pos + 18), u16(pos + 26)
        if (not 0 < u16(pos + 4) <= 20 or u16(pos + 6) & ~0x800 or
                u16(pos + 8) or u16(pos + 28) or u32(pos + 22) != size or
                not 1 <= length <= 95 or 30 + length > central - pos or size > central - pos - 30 - length):
            raise ValueError("Only plain ZIP_STORED entries without extras or descriptors are supported")
        name = data[pos + 30:pos + 30 + length].decode("ascii")
        if (name not in FILES and not _asset_path(name)) or name in content:
            raise ValueError("Unexpected or duplicate ZIP member")
        limit = MAX_MANIFEST if name == "manifest.json" else MAX_ICON_PIXELS + 8 if name == "icon.bin" else 32768 if name in FILES else MAX_IMAGE_BYTES + 8
        if (name in FILES and size == 0) or size > limit:
            raise ValueError(f"{name}: exceeds its file size limit")
        payload = data[pos + 30 + length:pos + 30 + length + size]
        if crc(payload) != u32(pos + 14):
            raise ValueError(f"{name}: CRC mismatch")
        if name in ("manifest.json", "page.json", "app.js"):
            _utf8(payload, name)
        content[name], locals_[name] = payload, pos
        pos += 30 + length + size
    if pos != central or not set(FILES).issubset(content):
        raise ValueError("Invalid ZIP local file layout")
    seen = set()
    for _ in range(count):
        if end - pos < 46 or u32(pos) != 0x02014b50:
            raise ValueError("Invalid ZIP central header")
        length = u16(pos + 28)
        if not 1 <= length <= 95 or 46 + length > end - pos or u16(pos + 30) or u16(pos + 32) or u16(pos + 34):
            raise ValueError("ZIP central extras, comments and split archives are not supported")
        name = data[pos + 46:pos + 46 + length].decode("ascii")
        if name not in content or name in seen:
            raise ValueError("Unexpected or duplicate ZIP central member")
        seen.add(name)
        local, mode = locals_[name], (u32(pos + 38) >> 16) & 0o170000
        if (mode not in (0, 0o100000) or u32(pos + 38) & 0x10 or u32(pos + 42) != local or
                u16(pos + 6) != u16(local + 4) or u16(pos + 8) != u16(local + 6) or u16(pos + 10) or
                u16(pos + 12) != u16(local + 10) or u16(pos + 14) != u16(local + 12) or
                u32(pos + 16) != u32(local + 14) or u32(pos + 20) != len(content[name]) or
                u32(pos + 24) != len(content[name])):
            raise ValueError("ZIP central/local headers disagree or member is not a regular file")
        pos += 46 + length
    if pos != end or seen != set(content):
        raise ValueError("Invalid ZIP central directory layout")
    return content


def _utf8(data, name):
    if b"\0" in data:
        raise ValueError(f"{name}: embedded NUL is not supported")
    data.decode("utf-8")


def _json(data):
    def reject_constant(value):
        raise ValueError(f"Non-JSON number {value}")
    return json.loads(data.decode("utf-8"), parse_constant=reject_constant)


def _number(value, expected):
    return type(value) in (int, float) and value == expected


def _name(value, field):
    if not isinstance(value, str) or "\0" in value or not 0 < len(value.encode("utf-8")) < 48:
        raise ValueError(f"{field} must be 1..47 UTF-8 bytes without NUL")


def _image(data, side=MAX_IMAGE_SIDE, maximum=MAX_IMAGE_BYTES, name="Image"):
    if len(data) < 8 or data[:4] != b"OIC1":
        raise ValueError(f"{name} requires an OIC1 image header")
    width, height = struct.unpack_from("<HH", data, 4)
    pixels = width * height * 4
    if not (1 <= width <= side and 1 <= height <= side and
            pixels <= maximum and len(data) == 8 + pixels):
        raise ValueError(f"{name} requires dimensions 1..{side}, at most {maximum} pixel bytes and exactly width*height*4 BGRA bytes")
    return pixels


def _icon(data):
    return _image(data, MAX_ICON_SIDE, MAX_ICON_PIXELS, "icon.bin")


def convert_image(source, icon=False):
    try:
        from PIL import Image
    except ImportError:
        raise RuntimeError("PNG conversion needs Pillow in this Python; converted OIC1 binary images need only the stdlib") from None
    side, maximum = (MAX_ICON_SIDE, MAX_ICON_PIXELS) if icon else (MAX_IMAGE_SIDE, MAX_IMAGE_BYTES)
    with Image.open(source) as image:
        width, height = image.size
        if image.format != "PNG" or not (1 <= width <= side and 1 <= height <= side) or width * height * 4 > maximum:
            raise ValueError(f"PNG dimensions must be 1..{side} per side and at most {maximum} pixel bytes")
        data = b"OIC1" + struct.pack("<HH", width, height) + image.convert("RGBA").tobytes("raw", "BGRA")
    _image(data, side, maximum, str(source))
    return data


def convert_icon(source):
    return convert_image(source, icon=True)


def validate(data):
    content = _unzip(data)
    manifest = _json(content["manifest.json"])
    if (not isinstance(manifest, dict) or manifest.get("runtime") != "ouse-core" or
            "format" in manifest or not _number(manifest.get("api"), 1)):
        raise ValueError("Requires runtime=ouse-core, api=1, without a format field")
    app_id = manifest.get("id")
    if not isinstance(app_id, str) or not re.fullmatch(r"user\.[a-z0-9._-]+", app_id) or len(app_id) >= 48:
        raise ValueError("Application ID must be user.* (ASCII, max 47 bytes)")
    _name(manifest.get("name"), "name")
    if manifest.get("icon") != "icon.bin":
        raise ValueError("BPK requires icon=icon.bin inside the archive")
    names = manifest.get("names", {})
    if not isinstance(names, dict) or set(names) - {"zh_CN", "en"}:
        raise ValueError("names must be an object containing only zh_CN and/or en")
    for locale, value in names.items():
        _name(value, f"names.{locale}")
    capabilities = manifest.get("capabilities", {})
    if (not isinstance(capabilities, dict) or set(capabilities) - {"background", "notifications", "network", "media", "alarms"} or
            any(type(value) is not bool for value in capabilities.values())):
        raise ValueError("capabilities supports only background/notifications/network/media/alarms: true/false")
    _icon(content["icon.bin"])
    assets = _assets(manifest)
    if set(content) != set(FILES) | {asset["path"] for asset in assets}:
        raise ValueError("ZIP must contain exactly the four fixed files and every declared asset")
    image_bytes = data_bytes = 0
    for asset in assets:
        payload, kind = content[asset["path"]], asset["type"]
        if kind == "image":
            image_bytes += _image(payload, name=asset["path"])
        else:
            limit = 16384 if kind == "text" else 65536
            if len(payload) > limit:
                raise ValueError(f"{asset['path']}: requires at most {limit} bytes")
            if kind == "text":
                _utf8(payload, asset["path"])
            data_bytes += len(payload)
    if image_bytes > MAX_IMAGE_BYTES or data_bytes > MAX_DATA_BYTES:
        raise ValueError("Image pixels and text/binary assets each have a total limit of 131072 bytes")
    page = _json(content["page.json"])
    if not isinstance(page, dict) or not _number(page.get("api"), 1) or ("root" in page) == ("pages" in page):
        raise ValueError("page.json requires api=1 and exactly one of root or pages")
    if "root" in page:
        if not isinstance(page["root"], dict):
            raise ValueError("page.json root must be an object")
    else:
        pages = page["pages"]
        if not isinstance(pages, list) or not 1 <= len(pages) <= 16:
            raise ValueError("page.json pages must contain 1..16 pages")
        ids = set()
        for item in pages:
            if (not isinstance(item, dict) or not isinstance(item.get("id"), str) or
                    not re.fullmatch(r"[A-Za-z][A-Za-z0-9_-]{0,30}", item["id"]) or
                    item["id"] in ids or not isinstance(item.get("root"), dict)):
                raise ValueError("Each page needs a unique ASCII ID (1..31 characters) and a root object")
            ids.add(item["id"])
        if not isinstance(page.get("initialPage"), str) or page["initialPage"] not in ids:
            raise ValueError("page.json initialPage must name one of the declared pages")
    return manifest


def pack(source, output):
    import io
    manifest_data = (source / "manifest.json").read_bytes()
    manifest = _json(manifest_data)
    if not isinstance(manifest, dict) or "format" in manifest:
        raise ValueError("manifest.json requires an object without a format field")
    content = {"manifest.json": manifest_data,
               "page.json": (source / "page.json").read_bytes(),
               "app.js": (source / "app.js").read_bytes()}
    icon_name = manifest.get("icon")
    if icon_name == "icon.png":
        content["icon.bin"] = convert_icon(source / "icon.png")
    elif icon_name == "icon.bin":
        content["icon.bin"] = (source / "icon.bin").read_bytes()
    else:
        raise ValueError("Source icon must be icon.png or icon.bin")
    manifest["icon"] = "icon.bin"
    for asset in _assets(manifest):
        name = asset["path"]
        source_path = (source / name).resolve()
        if not source_path.is_relative_to(source.resolve()) or not source_path.is_file():
            raise ValueError(f"{name}: requires a file within the application source directory")
        if asset["type"] == "image" and source_path.suffix.lower() == ".png":
            payload = convert_image(source_path)
            name = str(Path(name).with_suffix(".bin")).replace("\\", "/")
            asset["path"] = name
        else:
            payload = source_path.read_bytes()
        if name in content:
            raise ValueError("Converted asset paths must be unique")
        content[name] = payload
    _assets(manifest)
    content["manifest.json"] = json.dumps(manifest, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w", compression=zipfile.ZIP_STORED) as z:
        for name, payload in content.items():
            info = zipfile.ZipInfo(name, (2026, 1, 1, 0, 0, 0))
            info.external_attr = 0o100644 << 16
            z.writestr(info, payload)
    data = buffer.getvalue()
    manifest = validate(data)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(data)
    print(f"Packed {manifest['id']}: {output} ({len(data)} bytes, {len(manifest.get('assets', []))} assets)")
    return manifest


def wire(token, sequence, verb, payload):
    body = f"OPK1 {token:08x} {sequence:08x} {verb} {payload}".encode("ascii")
    return body + f" {crc(body):08x}\n".encode("ascii")


def parse_reply(line, token, sequence):
    # Boot logs may precede a frame; only accept a matching token/sequence + CRC.
    start = line.find(b"OPK1 ")
    if start < 0:
        return None
    try:
        body, checksum = line[start:].strip().rsplit(b" ", 1)
        if len(checksum) != 8 or int(checksum, 16) != crc(body):
            return None
        magic, tok, seq, status, payload = body.decode("ascii").split(" ")
        if int(tok, 16) != token or int(seq, 16) != sequence or status not in ("OK", "ERR"):
            return None
        return status, payload
    except (ValueError, UnicodeError):
        return None


class Client:
    def __init__(self, port, baud=921600):
        try:
            import serial
        except ImportError:
            raise RuntimeError('Install serial support: python -m pip install "pyserial==3.5"') from None
        # Set control lines BEFORE opening; never trigger the board boot/reset.
        self.serial = serial.Serial(port=None, baudrate=int(baud), timeout=0.15,
                                    write_timeout=5, rtscts=False, dsrdtr=False)
        self.serial.dtr = False
        self.serial.rts = False
        self.serial.port = port
        self.serial.open()
        self.serial.reset_input_buffer()
        self.token = secrets.randbits(32)
        self.sequence = 0
        self.pending = bytearray()
        self.serial.write(b"\n")  # resynchronize a previous partial line

    def close(self):
        self.serial.close()

    def request(self, verb, payload="-", timeout=4, attempts=3):
        self.sequence += 1
        request = wire(self.token, self.sequence, verb, payload)
        for _ in range(attempts):
            self.serial.write(request)
            self.serial.flush()
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                data = self.serial.read(1024)
                self.pending.extend(data)
                while b"\n" in self.pending:
                    line, _, tail = self.pending.partition(b"\n")
                    self.pending = bytearray(tail)
                    reply = parse_reply(line, self.token, self.sequence)
                    if reply:
                        status, result = reply
                        if status == "ERR":
                            raise RuntimeError(f"{verb}: {result}")
                        return result
                if len(self.pending) > 8192:
                    self.pending.clear()
        raise TimeoutError(f"{verb}: no reply; check COM port, close serial monitor, wait for OuseUSB READY")

    def install(self, data):
        manifest = validate(data)
        self.request("BEGIN", f"{len(data):08x}:{crc(data):08x}")
        try:
            for offset in range(0, len(data), 512):
                chunk = data[offset:offset+512]
                result = self.request("DATA", f"{offset:08x}:{chunk.hex()}")
                if int(result) != offset + len(chunk):
                    raise RuntimeError("Unexpected received offset")
                print(f"\rUploading {offset+len(chunk)}/{len(data)}", end="", flush=True)
            print()
            result = self.request("END", timeout=20)
            if result != manifest["id"]:
                raise RuntimeError(f"Unexpected installed ID: {result}")
        except Exception:
            try:
                self.request("ABORT", attempts=1)
            except Exception:
                pass
            raise
        print(f"Installed {result}; desktop refreshed, package persists across reboot")
        return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="USB bridge serial port, e.g. COM10")
    parser.add_argument("--baud", type=int, default=921600, help="UART baud rate; must match the firmware")
    commands = parser.add_subparsers(dest="command", required=True)
    command = commands.add_parser("pack")
    command.add_argument("source", type=Path)
    command.add_argument("output", type=Path)
    command = commands.add_parser("convert-icon", help="Explicitly convert a PNG to OIC1 icon.bin using Pillow")
    command.add_argument("source", type=Path)
    command.add_argument("output", type=Path)
    command = commands.add_parser("install")
    command.add_argument("bpk", type=Path)
    command.add_argument("--open", action="store_true")
    commands.add_parser("hello")
    commands.add_parser("list")
    commands.add_parser("sync", help="List installed packages and live/foreground app ids")
    for verb in ("open", "remove"):
        command = commands.add_parser(verb)
        command.add_argument("id")
    args = parser.parse_args()
    if args.command == "pack":
        pack(args.source, args.output)
        return
    if args.command == "convert-icon":
        data = convert_icon(args.source)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(data)
        print(f"Converted {args.source}: {args.output} ({len(data)} bytes)")
        return
    if not args.port:
        parser.error("--port COMxx is required")
    data = args.bpk.read_bytes() if args.command == "install" else None
    if data is not None:
        validate(data)  # no serial access for a known-invalid package
    client = Client(args.port, args.baud)
    try:
        hello = client.request("HELLO", timeout=10)
        if args.command == "sync":
            installed = [item for item in client.request("LIST").split(",") if item]
            active = ""
            alive = []
            if "debug=1" in hello:
                state = json.loads(bytes.fromhex(client.request("DBG_STATE")).decode("utf-8"))
                active = state.get("active") or ""
                alive = state.get("alive") if isinstance(state.get("alive"), list) else ([active] if active else [])
            print(json.dumps({"installed": installed, "active": active, "alive": alive}, ensure_ascii=False))
            return
        print(hello)
        if args.command == "hello":
            return
        if args.command == "install":
            app_id = client.install(data)
            if args.open:
                client.request("OPEN", app_id)
        elif args.command == "list":
            print(client.request("LIST").replace(",", "\n"))
        else:
            if not re.fullmatch(r"user\.[a-z0-9._-]{1,42}", args.id):
                parser.error("Application ID must be user.*")
            print(client.request(args.command.upper(), args.id))
    finally:
        client.close()


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError, zipfile.BadZipFile) as error:
        print(f"Error: {error}", file=sys.stderr)
        sys.exit(1)
