"""Check device rejection diagnostics without serial hardware."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("bpk", Path(__file__).with_name("bpk.py"))
bpk = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bpk)


class Transport:
    def __init__(self, data):
        self.data = data

    def write(self, data):
        return len(data)

    def flush(self):
        pass

    def read(self, size):
        result, self.data = self.data[:size], self.data[size:]
        return result


class DiagnosticsTests(unittest.TestCase):
    def client(self, data):
        client = bpk.Client.__new__(bpk.Client)
        client.serial = Transport(data)
        client.token = 1
        client.sequence = 0
        client.pending = bytearray()
        return client

    def test_device_rejection_includes_stage(self):
        data = b"\x1b[0;31mE (100) OuseUSB: BPK rejected: archive/manifest (ESP_ERR_INVALID_ARG)\x1b[0m\n"
        client = self.client(data + bpk.wire(1, 1, "ERR", "ESP_ERR_INVALID_ARG"))
        with self.assertRaises(RuntimeError) as failure:
            client.request("END")
        self.assertIn("END: ESP_ERR_INVALID_ARG", str(failure.exception))
        self.assertIn("archive/manifest", str(failure.exception))
        self.assertNotIn("\x1b", str(failure.exception))

    def test_success_ignores_logs(self):
        client = self.client(b"I (100) OuseUSB: ready\n" + bpk.wire(1, 1, "OK", "user.counter"))
        self.assertEqual(client.request("END"), "user.counter")

    def test_stage_survives_without_device_logs(self):
        client = self.client(bpk.wire(1, 1, "ERR", "ESP_ERR_INVALID_ARG:upload-integrity"))
        with self.assertRaisesRegex(RuntimeError, "END: ESP_ERR_INVALID_ARG:upload-integrity"):
            client.request("END")

    def test_diagnostics_are_bounded(self):
        data = b"".join(f"E OuseJS: failure {index}\n".encode() for index in range(20))
        client = self.client(data + bpk.wire(1, 1, "ERR", "ESP_ERR_INVALID_ARG"))
        with self.assertRaises(RuntimeError) as failure:
            client.request("END")
        self.assertNotIn("failure 0\n", str(failure.exception))
        self.assertEqual(str(failure.exception).count("OuseJS:"), 12)


if __name__ == "__main__":
    unittest.main()
