#!/usr/bin/env python3
"""Stage20-D strict CLOG validation: genuine post-VBAR vector evidence."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

from test_cactus_usb_decode import frame

ROOT = Path(__file__).resolve().parent


def sample(*, override=None, missing=None, bad_crc=False):
    events = [0x1900, 0x1901, 0x1910, 0x1912, 0x1911, 0x1930]
    for i in range(10):
        events.extend((0x1940, 0x1941))
    events.extend((0x1950, 0x1951, 0x1952, 0x20d6, 0x20d0,
                   0x20a0, 0x20a1, 0x20d1, 0x20d2, 0x20d3,
                   0x20c2, 0x20b3, 0x20a3, 0x20d5, 0x20ff, 0x19ff))
    if missing is not None:
        events.remove(missing)
    content = []
    seg = 0
    for index, event in enumerate(events):
        b = bytearray(frame(index, event, stage=23))
        values = {
            0x1901: (1, 0, 0),
            0x1910: (1, 0, 0),
            0x1911: (0, 0, 0),
            0x20d6: (0x8142e000, 0x8142e200, 0x50800000),
            0x20d0: (0x8142d488, 0x8142f154, 0x14000719),
            0x20a1: (4, 0x8142d488, 0x824bc000),
            0x20d1: ((0x3c << 26) | 0xd20, 0x8142f154, 0),
            0x20d2: (0x8142e000, 0x824bc000, 0xfffffff006000000),
            0x20d3: (0x80000000, 0x1e000000, 0x1e000000),
            0x20c2: (0x824bc000, 0xffffffff, 0xffffffff),
            0x20b3: ((0x16 << 26) | 0x21, 0x41e38060, 0x3c5),
            0x20a3: (8, 0, 0),
            0x20d5: (1, 1, 1),
            0x20ff: (0, 0, 0),
            0x19ff: (0, 0, 0)
        }
        if event in (0x1940, 0x1941):
            values[event] = (seg, 0, 0)
            if event == 0x1941:
                seg += 1
        if override and event == override[0]:
            values[event] = override[1]
        if event in values:
            struct.pack_into("<QQQ", b, 28, *values[event])
        if event in (0x20a1, 0x20d1, 0x20d2, 0x20d3, 0x20c2):
            b[7] = 4
        struct.pack_into("<I", b, 52, zlib.crc32(b[:52]))
        if bad_crc and event == 0x20d1:
            b[52] ^= 1
        content.append(bytes(b))
    return b"".join(content)


class Gate(unittest.TestCase):
    def test_positive_and_negative_cases(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "out/cactus") as folder:
            capture = Path(folder) / "stage20d.bin"
            cmd = [sys.executable, str(ROOT / "decode-cactus-usb.py"),
                   str(capture), "--require-stage20d"]

            def run(blob):
                capture.write_bytes(blob)
                return subprocess.run(cmd, text=True, capture_output=True)

            success = run(sample())
            self.assertEqual(success.returncode, 0, success.stdout+success.stderr)
            self.assertIn("POST-VBAR EL1h VECTOR PASS", success.stdout)
            failures = [
                sample(override=(0x20d1, ((0x3c << 26) | 0xd21,
                                          0x8142f154, 0))),
                sample(override=(0x20d2, (0x8142e200, 0x824bc000,
                                           0xfffffff006000000))),
                sample(override=(0x20d3, (0x80000000, 0x1e000001,
                                           0x1e000000))),
                sample(override=(0x20c2, (0x824bc000, 0xffffffff, 0))),
                sample(override=(0x20d5, (1, 0, 1))),
                sample(missing=0x20d6),
                sample(missing=0x20b3),
                sample(bad_crc=True),
                sample()[:-56],
            ]
            for index, payload in enumerate(failures):
                result = run(payload)
                self.assertNotEqual(result.returncode, 0,
                                    f"invalid capture {index} accepted: {result.stdout}")


if __name__ == "__main__":
    unittest.main()
