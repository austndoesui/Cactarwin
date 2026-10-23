#!/usr/bin/env python3
"""Strict Stage25 CLOG gate: translated vector, MMU state and recovery."""
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

from test_cactus_usb_decode import frame

ROOT = Path(__file__).resolve().parent


def capture(*, override=None, missing=None, bad_crc=False):
    events = [0x1900, 0x1901, 0x1910, 0x1912, 0x1911, 0x1930]
    for _ in range(10):
        events.extend((0x1940, 0x1941))
    events.extend((0x1950, 0x1951, 0x1952, 0x21d6, 0x21d0,
                   0x20a0, 0x21da, 0x20a1, 0x21d1, 0x21d2,
                   0x21d3, 0x21d4, 0x21d8, 0x21d5, 0x20b3,
                   0x20a3, 0x21d7, 0x20ff, 0x19ff))
    if missing is not None:
        events.remove(missing)
    values = {
        0x1901: (1, 0, 0),
        0x1910: (1, 0, 0),
        0x1911: (0, 0, 0),
        0x21d6: (0x81429000, 0xfffffff007429000, 0xd4000422),
        0x21d0: (0x8142d488, 0x8142f490, 0x14000719),
        0x21da: (0x8142f490, 0xfffffff007429000, 0),
        0x20a1: (4, 0x8142d488, 0x824bc000),
        0x21d1: (0xf2000e22, 0x8142f490, 0),
        0x21d2: (0xfffffff007429000, 0x22a5192519, 0x34d4593d),
        0x21d3: (0x8117a000, 0x8117b000, 0x0c08044f00bb44ff),
        0x21d4: (0x8117c003, 0x8117d003, 0x81000601),
        0x21d8: (0x81000601, 0x8117e003, 0x9dc00601),
        0x21d5: (0xfffffff007429004, 0x3c5, 0x3c4),
        0x20b3: (0x5a000021, 0xfffffff007429004, 0x3c5),
        0x20a3: (8, 0, 0),
        0x21d7: (1, 1, 1),
        0x20ff: (0, 0, 0),
        0x19ff: (0, 0, 0),
    }
    chunks = []
    segment = 0
    for i, event in enumerate(events):
        raw = bytearray(frame(i, event, stage=25))
        entry = values.get(event)
        if event in (0x1940, 0x1941):
            entry = (segment, 0, 0)
            if event == 0x1941:
                segment += 1
        if override and event == override[0]:
            entry = override[1]
        if entry is not None:
            struct.pack_into("<QQQ", raw, 28, *entry)
        if event == 0x20a1:
            raw[7] = 4
        struct.pack_into("<I", raw, 52, zlib.crc32(raw[:52]))
        if bad_crc and event == 0x21d1:
            raw[52] ^= 1
        chunks.append(bytes(raw))
    return b"".join(chunks)


class CLOG(unittest.TestCase):
    def test_stage25_gate_and_corruptions(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "out/cactus") as folder:
            target = Path(folder) / "stage25.bin"
            args = [sys.executable, str(ROOT / "decode-cactus-usb.py"),
                    str(target), "--require-stage21c2"]

            def run(data):
                target.write_bytes(data)
                return subprocess.run(args, capture_output=True, text=True)

            good = run(capture())
            self.assertEqual(good.returncode, 0, good.stdout + good.stderr)
            self.assertIn("POST-MMU EL1t VIRTUAL-VECTOR GATE PASS", good.stdout)
            invalid = [
                capture(missing=0x21d1),
                capture(missing=0x21da),
                capture(override=(0x21d6, (0x8142e000, 0xfffffff007429000, 0xd4000422))),
                capture(override=(0x21d1, (0xf2000e21, 0x8142f490, 0))),
                capture(override=(0x21d2, (0xfffffff007429000, 0x22a5192519, 0x34d4593c))),
                capture(override=(0x21d2, (0x81429000, 0x22a5192519, 0x34d4593d))),
                capture(override=(0x21d4, (0, 0x8117d003, 0x81000601))),
                capture(override=(0x21d5, (0x81429004, 0x3c5, 0x3c4))),
                capture(override=(0x21d5, (0xfffffff007429004, 0x3c5, 0x3c5))),
                capture(override=(0x21d7, (0, 1, 1))),
                capture(bad_crc=True),
                capture()[:-56],
            ]
            for index, blob in enumerate(invalid):
                result = run(blob)
                self.assertNotEqual(result.returncode, 0,
                                    f"invalid capture {index} accepted")


if __name__ == "__main__":
    unittest.main()
