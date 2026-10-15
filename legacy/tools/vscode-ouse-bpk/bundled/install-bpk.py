"""Install a BPK with the official protocol, without deliberately pulsing reset lines.

Usage: python tools/install-bpk.py --port COM10 examples/music/dist/net.0use.music-1.1.0.bpk
Requires the installed brookesia-usb package. No upstream files are changed.
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import sys


def positive_timeout(value: str) -> float:
    number = float(value)
    if not math.isfinite(number) or number <= 0:
        raise argparse.ArgumentTypeError("timeout must be a finite positive number")
    return number


def make_client_type(upstream, serial_module):
    """Adapt only construction; inherit upstream handshake, transfer and cleanup.

    brookesia-usb 0.1.1 initializes these session fields in UsbClient.__init__.
    Keep this small adapter covered by tests when upgrading that dependency.
    """
    class InstallClient(upstream.UsbClient):
        def __init__(self, port: str, baudrate: int, timeout: float):
            self.port = port
            self.timeout = timeout
            self.request_id = 1
            self.session_active = False
            self._hello_request_pending = False
            self.serial = serial_module.Serial(
                port=None, baudrate=baudrate, timeout=timeout, write_timeout=timeout,
                rtscts=False, dsrdtr=False,
            )
            try:
                # With port=None, no handle is open while these states are set.
                self.serial.dtr = False
                self.serial.rts = False
                self.serial.port = port
                self.serial.open()
            except BaseException:
                self.serial.close()
                raise

    return InstallClient


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(
        description="Install a BPK while avoiding deliberate DTR/RTS reset pulses."
    )
    parser.add_argument("package", type=Path, help="existing .bpk file; not rebuilt")
    parser.add_argument("--port", required=True, help="device port, e.g. COM10")
    parser.add_argument("--baudrate", type=int, default=115200)
    parser.add_argument("--timeout", type=positive_timeout, default=10.0)
    args = parser.parse_args(argv)
    package = args.package.expanduser().resolve()
    if not package.is_file() or package.suffix.lower() != ".bpk":
        parser.error(f"package must be an existing .bpk file: {package}")
    port = args.port.strip()
    if not port:
        parser.error("--port must not be empty")
    if args.baudrate <= 0:
        parser.error("--baudrate must be positive")

    try:
        from brookesia_usb import cli as upstream
        import serial
    except ImportError as error:
        print(f"Missing dependency: {error}. Install brookesia-usb with this Python interpreter.",
              file=sys.stderr)
        return 1

    client_type = make_client_type(upstream, serial)
    try:
        print(f"Connecting to {port} at {args.baudrate} baud...", flush=True)
        with client_type(port, args.baudrate, args.timeout) as client:
            client.hello()
            print(f"Installing {package.name}...", flush=True)
            # Keep the upstream upload, SHA-256 verification and installer protocol.
            result = client.transfer("install_bpk", package, None, False)
        print(json.dumps(result, indent=2, ensure_ascii=False))
        print("Installation succeeded; serial port released.")
        return 0
    except KeyboardInterrupt:
        print("Installation interrupted; serial port released.", file=sys.stderr)
        return 130
    except (OSError, RuntimeError, TimeoutError, ValueError) as error:
        print(f"Installation failed: {error}", file=sys.stderr)
        if isinstance(error, serial.SerialException):
            print("Check the port and close any serial monitor using it.", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
