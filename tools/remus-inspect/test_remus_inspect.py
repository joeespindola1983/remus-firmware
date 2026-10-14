import importlib.util
import io
import struct
import sys
import unittest
from pathlib import Path


SCRIPT = Path(__file__).with_name("remus_inspect.py")
SPEC = importlib.util.spec_from_file_location("remus_inspect", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


def session_header():
    raw = bytearray(64)
    struct.pack_into("<4sHHQQIQIIqII", raw, 0, b"RMS1", 1, 64, 11, 22, 33,
                     44, 55, 66, 0, 77, 0)
    struct.pack_into("<I", raw, 60, MODULE.crc32c(raw[:60]))
    return bytes(raw)


def imu_record():
    data_batch = struct.pack("<QIHHHhhhhhhH", 100, 3, 1, 5000, 0,
                             1, 2, 3, 4, 5, 6, 0)
    payload = struct.pack("<QQbBH", 500, 600, -42, 0, 0) + data_batch
    total = 48 + len(payload) + 4
    raw = bytearray(total)
    struct.pack_into("<4sBBHIQQQIII", raw, 0, b"RMR1", 1, 3, 48, total,
                     1, 700, 88, 99, 3, len(payload))
    raw[48:-4] = payload
    struct.pack_into("<I", raw, total - 4, MODULE.crc32c(raw[:-4]))
    return bytes(raw)


class InspectorTest(unittest.TestCase):
    def test_validates_and_decodes_imu(self):
        stream = io.BytesIO(session_header() + imu_record())
        header = MODULE.read_header(stream)
        records = list(MODULE.records(stream))
        self.assertEqual(header.computer_id, 22)
        self.assertEqual(len(records), 1)
        sample = list(MODULE.decode_imu(records[0]))[0]
        self.assertEqual(sample[0], 100)
        self.assertEqual(sample[3], -42)
        self.assertEqual(sample[6:12], (1, 2, 3, 4, 5, 6))

    def test_truncated_tail_preserves_valid_prefix(self):
        stream = io.BytesIO(session_header() + imu_record()[:-2])
        MODULE.read_header(stream)
        self.assertEqual(list(MODULE.records(stream)), [])

    def test_rejects_corrupt_header(self):
        raw = bytearray(session_header())
        raw[16] ^= 1
        with self.assertRaises(MODULE.RmsError):
            MODULE.read_header(io.BytesIO(raw))


if __name__ == "__main__":
    unittest.main()
