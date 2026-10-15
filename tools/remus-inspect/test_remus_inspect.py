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

    def test_decodes_source_manifest_conversion_metadata(self):
        payload = struct.pack("<QIHBxHHHBxHBBHHHI", 88, 99, 0x70, 8, 500, 42,
                              200, 0, 0, 0, 0, 0, 4096, 655, 0)
        record = MODULE.Record(64, MODULE.SOURCE_MANIFEST, 1, 0, 88, 99, 0, payload)
        manifest = MODULE.decode_manifest(record)
        self.assertEqual(manifest["lsb_per_g"], 4096)
        self.assertEqual(manifest["lsb_per_dps_x10"], 655)

    def test_decodes_explicit_gap_with_unknown_last_sample(self):
        payload = struct.pack("<IIQQHH", 7, 9, 70, 0xFFFFFFFFFFFFFFFF, 2, 1)
        record = MODULE.Record(64, MODULE.GAP, 1, 0, 88, 99, 7, payload)
        gap = MODULE.decode_gap(record)
        self.assertEqual(gap["from_batch"], 7)
        self.assertEqual(gap["through_batch"], 9)
        self.assertEqual(gap["first_sample"], 70)
        self.assertIsNone(gap["last_sample"])

    def test_rejects_invalid_explicit_gap(self):
        payload = struct.pack("<IIQQHH", 9, 7, 90, 70, 2, 0)
        record = MODULE.Record(64, MODULE.GAP, 1, 0, 88, 99, 9, payload)
        with self.assertRaises(MODULE.RmsError):
            MODULE.decode_gap(record)

    def test_decodes_gps_fix(self):
        payload = struct.pack("<QiiIHBBHHqI", 123456, -234567890, -456789012,
                              3210, 12345, 9, 1, 85, 0x7F,
                              1792000000123, 12)
        record = MODULE.Record(64, MODULE.GPS_FIX, 1, 123456, 22, 33, 7, payload)
        fix = MODULE.decode_gps(record)
        self.assertEqual(fix["latitude_e7"], -234567890)
        self.assertEqual(fix["satellites"], 9)
        self.assertEqual(fix["flags"], 0x7F)

    def test_rejects_invalid_gps_coordinate(self):
        payload = struct.pack("<QiiIHBBHHqI", 123456, 900000001, 0,
                              0xFFFFFFFF, 0xFFFF, 0xFF, 0, 0xFFFF,
                              0x02, 0, 0)
        record = MODULE.Record(64, MODULE.GPS_FIX, 1, 123456, 22, 33, 7, payload)
        with self.assertRaises(MODULE.RmsError):
            MODULE.decode_gps(record)


if __name__ == "__main__":
    unittest.main()
