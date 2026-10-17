"""Check immediate serial reads with fragmented replies and console noise."""
import bpk


class Serial:
    def __init__(self):
        self.response = bytearray()
        self.reads = []

    @property
    def in_waiting(self):
        return min(len(self.response), 13)

    def write(self, frame):
        body, checksum = frame.strip().rsplit(b" ", 1)
        assert bpk.crc(body) == int(checksum, 16)
        magic, token, sequence, verb, payload = body.decode().split(" ")
        assert magic == "OPK1" and verb == "HELLO" and payload == "-"
        self.response.extend(b"Console log\n")
        self.response.extend(bpk.wire(int(token, 16), int(sequence, 16), "OK", "ready=1"))

    def flush(self):
        pass

    def read(self, capacity):
        assert capacity <= max(self.in_waiting, 1)
        self.reads.append(capacity)
        data = self.response[:capacity]
        del self.response[:capacity]
        return data


client = bpk.Client.__new__(bpk.Client)
client.serial = Serial()
client.token = 1234
client.sequence = 0
client.pending = bytearray()
for index in range(3):
    assert client.request("HELLO") == "ready=1"
assert len(client.serial.reads) > 3 and not client.pending
print("PASS BPK available-byte reads, fragmented matching replies and console noise")
