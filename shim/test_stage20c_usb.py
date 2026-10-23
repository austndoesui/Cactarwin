#!/usr/bin/env python3
"""Stage20-C CRC trace gate: prove framebuffer marker before VBAR handoff."""
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent
from test_cactus_usb_decode import frame


def sample(*, invalid_trap=False, invalid_fb=False, missing=False):
    events = [0x1900, 0x1901, 0x1910, 0x1912, 0x1911, 0x1930]
    for i in range(10):
        events.extend((0x1940, 0x1941))
    events.extend((0x1950, 0x1951, 0x1952, 0x20c0, 0x20a0,
                   0x20a1, 0x20c1, 0x20c2, 0x20b3, 0x20a3,
                   0x20c3, 0x20ff, 0x19ff))
    if missing:
        events.remove(0x20c2)
    result = []
    seg = 0
    for index, event in enumerate(events):
        b = bytearray(frame(index, event, stage=22))
        if event in (0x1901, 0x1910):
            struct.pack_into("<Q", b, 28, 1)
        elif event in (0x1911, 0x19ff, 0x20ff):
            struct.pack_into("<Q", b, 28, 0)
        elif event in (0x1940, 0x1941):
            struct.pack_into("<Q", b, 28, seg)
            if event == 0x1941:
                seg += 1
        elif event == 0x20c0:
            struct.pack_into("<QQQ", b, 28, 0x8142d488,
                             0x8142f144, 0x14000719)
        elif event == 0x20a1:
            b[7] = 4
            struct.pack_into("<QQQ", b, 28, 4, 0x8142d488,
                             0x824bc000)
        elif event == 0x20c1:
            b[7] = 4
            struct.pack_into("<QQQ", b, 28,
                             (0x3c << 26) | (0xc21 if invalid_trap else 0xc20),
                             0x8142f144, 0)
        elif event == 0x20c2:
            b[7] = 4
            struct.pack_into("<QQQ", b, 28, 0x824bc000,
                             0xffffffff, 0 if invalid_fb else 0xffffffff)
        elif event == 0x20b3:
            struct.pack_into("<QQQ", b, 28, (0x16 << 26) | 0x21,
                             0x41e38060, 0x3c5)
        elif event == 0x20a3:
            struct.pack_into("<Q", b, 28, 8)
        elif event == 0x20c3:
            struct.pack_into("<QQQ", b, 28, 1, 1, 1)
        struct.pack_into("<I", b, 52, zlib.crc32(b[:52]))
        result.append(bytes(b))
    return b"".join(result)


class Gate(unittest.TestCase):
    def test_success_and_rejection_cases(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "out/cactus") as folder:
            capture = Path(folder) / "stage20c.bin"
            cmd = [sys.executable, str(ROOT / "decode-cactus-usb.py"),
                   str(capture), "--require-stage20c"]
            capture.write_bytes(sample())
            run = subprocess.run(cmd, capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("EARLY XNU FRAMEBUFFER/VBAR GATE PASS", run.stdout)
            for broken in (sample(invalid_trap=True),
                           sample(invalid_fb=True), sample(missing=True),
                           sample()[:-56]):
                capture.write_bytes(broken)
                run = subprocess.run(cmd, capture_output=True, text=True)
                self.assertNotEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == "__main__":
    unittest.main()
