"""Exercise the installer against the installed official client and a fake UART."""

import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import serial
from brookesia_usb import cli, protocol

spec = importlib.util.spec_from_file_location("install_bpk", Path(__file__).with_name("install-bpk.py"))
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


class FakeSerial:
    instances = []
    fail_open = False
    reject_hash = False
    answer_hello = True

    def __init__(self, port=None, **settings):
        if port is not None:
            raise AssertionError("Serial constructor would open the port too early")
        self.dtr = self.rts = True
        self.port = port
        self.timeout = settings["timeout"]
        self.is_open = False
        self.closed = False
        self.responses = []
        self.commands = []
        self.payload = bytearray()
        self.frames = 0
        self.instances.append(self)

    def open(self):
        if self.dtr is not False or self.rts is not False:
            raise AssertionError("Reset control lines were not configured before open")
        if self.fail_open:
            raise serial.SerialException("port unavailable")
        self.is_open = True

    def close(self):
        self.closed = True
        self.is_open = False

    def reset_input_buffer(self):
        self.responses.clear()

    def flush(self):
        pass

    def readline(self):
        return self.responses.pop(0) if self.responses else b""

    def reply(self, request_id, op, **extra):
        self.responses.append((json.dumps({"version": 1, "request_id": request_id,
                                          "op": op, "ok": True, **extra}) + "\n").encode())

    def write(self, data):
        if not self.is_open:
            raise AssertionError("write on closed port")
        if data.startswith(protocol.FRAME_MAGIC):
            frame = protocol.decode_frame(data)
            if frame.frame_type == protocol.FrameType.DATA:
                self.frames += 1
                self.payload.extend(frame.payload)
                self.reply(frame.request_id, "ack", sequence=frame.sequence)
            elif frame.frame_type == protocol.FrameType.END:
                digest = hashlib.sha256(self.payload).hexdigest()
                assert len(self.payload) == self.upload["size"]
                assert digest == self.upload["sha256"]
                if self.reject_hash:
                    self.reply(frame.request_id, "error", ok=False,
                               error_code="hash_mismatch", error="SHA-256 mismatch")
                else:
                    self.reply(frame.request_id, "done", size=len(self.payload), sha256=digest)
            else:
                raise AssertionError("unexpected frame")
        else:
            command = json.loads(data)
            self.commands.append(command["op"])
            if command["op"] == "hello":
                if self.answer_hello:
                    self.reply(command["request_id"], "hello", protocol_version=1,
                               transport="uart", session="exclusive")
            elif command["op"] == "install_bpk":
                self.upload = command
                self.reply(command["request_id"], "ready", max_frame_payload=1024)
            elif command["op"] == "goodbye":
                self.reply(command["request_id"], "done", command="goodbye")
            else:
                raise AssertionError(f"unexpected operation: {command['op']}")
        return len(data)


class InstallTests(unittest.TestCase):
    def setUp(self):
        FakeSerial.instances = []
        FakeSerial.fail_open = FakeSerial.reject_hash = False
        FakeSerial.answer_hello = True
        with tempfile.NamedTemporaryFile(
            suffix=".bpk", prefix=".test-install-", dir=Path(__file__).resolve().parent,
            delete=False,
        ) as package:
            self.package = Path(package.name)
            self.addCleanup(self.package.unlink)
            package.write(bytes(range(256)) * 12)
        patcher = mock.patch.object(serial, "Serial", FakeSerial)
        patcher.start()
        self.addCleanup(patcher.stop)

    def run_install(self, *extra):
        output = io.StringIO()
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
            code = installer.main(["--port", "COM10", str(self.package), *extra])
        return code, output.getvalue()

    def test_open_order_and_official_transfer_and_cleanup(self):
        code, output = self.run_install()
        self.assertEqual(code, 0, output)
        connection = FakeSerial.instances[0]
        self.assertEqual(connection.commands, ["hello", "install_bpk", "goodbye"])
        self.assertEqual(bytes(connection.payload), self.package.read_bytes())
        self.assertEqual(connection.frames, 3)
        self.assertTrue(connection.closed)
        self.assertIn("Installation succeeded", output)

    def test_transfer_error_is_failure_and_releases_session(self):
        FakeSerial.reject_hash = True
        code, output = self.run_install()
        self.assertEqual(code, 1)
        self.assertIn("hash_mismatch", output)
        self.assertNotIn("Installation succeeded", output)
        self.assertEqual(FakeSerial.instances[0].commands[-1], "goodbye")
        self.assertTrue(FakeSerial.instances[0].closed)

    def test_open_failure_closes_handle_without_handshake(self):
        FakeSerial.fail_open = True
        code, output = self.run_install()
        self.assertEqual(code, 1)
        self.assertIn("port unavailable", output)
        self.assertTrue(FakeSerial.instances[0].closed)
        self.assertEqual(FakeSerial.instances[0].commands, [])

    def test_hello_timeout_keeps_upstream_retry_and_cleanup(self):
        FakeSerial.answer_hello = False
        with mock.patch.object(cli, "HELLO_MAX_ATTEMPTS", 2):
            code, output = self.run_install()
        self.assertEqual(code, 1)
        self.assertIn("timed out", output)
        self.assertEqual(FakeSerial.instances[0].commands, ["hello", "hello", "goodbye"])
        self.assertTrue(FakeSerial.instances[0].closed)

    def test_bad_arguments_do_not_open_port(self):
        for args in (["--port", "COM10", str(self.package.with_suffix('.missing'))],
                     ["--port", "COM10", str(self.package), "--timeout", "nan"],
                     ["--port", "COM10", str(self.package), "--timeout", "0"],
                     [str(self.package)]):
            with self.subTest(args=args), contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as raised:
                    installer.main(args)
                self.assertEqual(raised.exception.code, 2)
        self.assertEqual(FakeSerial.instances, [])


if __name__ == "__main__":
    unittest.main()
