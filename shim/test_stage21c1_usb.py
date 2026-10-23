#!/usr/bin/env python3
"""Strict Stage24 CLOG gate: success, missing frames, wrong EL and bad tables."""
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
    events.extend((0x1950, 0x1951, 0x1952, 0x21c6, 0x21c0,
                   0x20a0, 0x21ca, 0x20a1, 0x21c1, 0x21c2, 0x21c3,
                   0x21c4, 0x21c7, 0x21c8, 0x21c9, 0x20b3,
                   0x20a3, 0x21c5, 0x20ff, 0x19ff))
    if missing is not None:
        events.remove(missing)
    values = {
        0x1901: (1, 0, 0),
        0x1910: (1, 0, 0),
        0x1911: (0, 0, 0),
        0x21c6: (0x8142e000, 0x8142e000, 0xd4000422),
        0x21c0: (0x8142d488, 0x8142f474, 0x14000719),
        0x21ca: (0x8142f474, 0x8142e000, 0),
        0x20a1: (4, 0x8142d488, 0x824bc000),
        0x21c1: (0xf2000e21, 0x8142f474, 0),
        0x21c2: (0x8142e000, 0x22a5192519, 0x30d00800),
        0x21c3: (0x8117a000, 0x8117b000, 0x0c08044f00bb44ff),
        0x21c4: (0x8117c003, 0x8117d003, 0x8117e003),
        0x21c7: (0x81000601, 0x81000601, 0x6000005e600605),
        0x21c8: (0x6000005e800605, 0, 0x9dc00601),
        0x21c9: (0x8142e004, 0x3c5, 0x3c4),
        0x20b3: (0x5a000021, 0x8142e004, 0x3c5),
        0x20a3: (8, 0, 0),
        0x21c5: (1, 1, 1),
        0x20ff: (0, 0, 0),
        0x19ff: (0, 0, 0),
    }
    chunks = []
    segment = 0
    for i, event in enumerate(events):
        raw = bytearray(frame(i, event, stage=24))
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
        if bad_crc and event == 0x21c1:
            raw[52] ^= 1
        chunks.append(bytes(raw))
    return b"".join(chunks)


class CLOG(unittest.TestCase):
    def test_stage24_gate(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "out/cactus") as folder:
            target = Path(folder) / "stage24.bin"
            args = [sys.executable, str(ROOT / "decode-cactus-usb.py"),
                    str(target), "--require-stage21c1"]

            def run(data):
                target.write_bytes(data)
                return subprocess.run(args, capture_output=True, text=True)

            good = run(capture())
            self.assertEqual(good.returncode, 0, good.stdout+good.stderr)
            self.assertIn("PRE-MMU EL1t PAGE-TABLE GATE PASS", good.stdout)
            invalid = [
                capture(missing=0x21c1),
                capture(missing=0x21ca),
                capture(missing=0x21c9),
                capture(override=(0x21c1,(0xf2000e22,0x8142f474,0))),
                capture(override=(0x21c2,(0x8142e200,0x22a5192519,0x30d00800))),
                capture(override=(0x21c2,(0x8142e000,0x22a5192519,0x30d00801))),
                capture(override=(0x21c3,(0x8117b000,0x8117a000,0))),
                capture(override=(0x21c4,(0x8117c003,0,0x8117e003))),
                capture(override=(0x21c8,(0x6000005e800605,1,0x9dc00601))),
                capture(override=(0x21c9,(0x8142e004,0x3c4,0x3c4))),
                capture(override=(0x21c9,(0x8142e004,0x3c5,0x3c5))),
                capture(override=(0x21c5,(0,1,1))),
                capture(bad_crc=True),
                capture()[:-56],
            ]
            for i, blob in enumerate(invalid):
                result = run(blob)
                self.assertNotEqual(result.returncode, 0,
                                    f"invalid capture {i} accepted")


if __name__ == "__main__":
    unittest.main()
