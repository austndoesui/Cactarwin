#!/usr/bin/env python3
"""Stage27 strict USB decoding: later checkpoint, early fault and corruption."""
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

from test_cactus_usb_decode import frame

HERE = Path(__file__).resolve().parent


def capture(*, fault=False, changes=None, omit=None, corrupt=False):
    events = [0x1900, 0x1901, 0x1910, 0x1912, 0x1911, 0x1930]
    for n in range(10):
        events.extend((0x1940, 0x1941))
    events.extend((0x1950, 0x1951, 0x1952, 0x2203, 0x2204,
                   0x20a0, 0x2200, 0x2201, 0x2202, 0x2205,
                   0x20a1, 0x2206, 0x2207, 0x2208, 0x2209,
                   0x220a, 0x20b3, 0x20a3, 0x220b, 0x20ff, 0x19ff))
    if omit is not None:
        events.remove(omit)
    target = 0xFFFFFFF007AC0C0C
    exception_pc = target - 0x200 if fault else target
    esr = 0x96000004 if fault else 0xF2000E24
    params = {
        0x1901: (1, 0, 0), 0x1910: (1, 0, 0), 0x1911: (0, 0, 0),
        0x2203: (0x81429000, 0x81429200, 0xFFFFFFF007429000),
        0x2204: (0x8142D488, 0x81AC0C0C, 0x14000719),
        0x20a0: (0x8142D488, 0x824BC000, 8),
        0x2200: (0x410FD034, 0x80000000, 8),
        0x2201: (13000000, 200000, 200000),
        0x2202: (0x80000000, 0x1E000000, 0x824C0000),
        0x2205: (0x81AC0C0C, target, 0),
        0x20a1: (4, 0x8142D488, 0x824BC000),
        0x2206: (esr, exception_pc, 0xFFFFFFF007900000),
        0x2207: (0xFFFFFFF007429000, 0x22A5192519, 0x34D4593D),
        0x2208: (0x8117A000, 0x8117B000, 0x0C08044F00BB44FF),
        0x2209: (0xFFFFFFF007429004, 0x3C5, 0x3C4),
        0x220A: (2 if fault else 1, exception_pc,
                 0x25 if fault else 0x3C),
        0x20b3: (0x5A000021, 0xFFFFFFF007429004, 0x3C5),
        0x20a3: (8, 0, 0), 0x220b: (1, 1, 1),
        0x20ff: (0, 0, 0), 0x19ff: (0, 0, 0),
    }
    params.update(changes or {})
    pieces = []
    segment = 0
    for i, event in enumerate(events):
        pkt = bytearray(frame(i, event, stage=27))
        values = params.get(event)
        if event in (0x1940, 0x1941):
            values = (segment, 0, 0)
            if event == 0x1941:
                segment += 1
        if values is not None:
            struct.pack_into("<QQQ", pkt, 28, *values)
        if event == 0x20a1:
            pkt[7] = 4
        struct.pack_into("<I", pkt, 52, zlib.crc32(pkt[:52]))
        if corrupt and event == 0x2206:
            pkt[53] ^= 0x01
        pieces.append(bytes(pkt))
    return b"".join(pieces)


class Stage22USB(unittest.TestCase):
    def test_checkpoint_fault_and_negative_variants(self):
        with tempfile.TemporaryDirectory(dir=HERE / "out/cactus") as folder:
            path = Path(folder) / "usb.bin"
            cmd = [sys.executable, str(HERE / "decode-cactus-usb.py"),
                   str(path), "--require-stage22", "--show", "0"]

            def run(payload):
                path.write_bytes(payload)
                return subprocess.run(cmd, text=True, capture_output=True)

            for fault, verdict in ((False, "DEEPER XNU C CHECKPOINT PASS"),
                                   (True, "FIRST XNU EXCEPTION CAPTURED")):
                proc = run(capture(fault=fault))
                self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)
                self.assertIn(verdict, proc.stdout)

            bad = [
                capture(omit=0x2200),
                capture(omit=0x2206),
                capture(changes={0x2201: (0, 12, 13)}),
                capture(changes={0x2202: (0x80000000, 0, 0)}),
                capture(changes={0x2203: (0, 0, 0)}),
                capture(changes={0x2204: (0, 0, 0)}),
                capture(changes={0x2206: (0xF2000E23, 0xFFFFFFF007AC0C0C, 0)}),
                capture(changes={0x220A: (2, 0xFFFFFFF007AC0C0C, 0x3C)}),
                capture(changes={0x220B: (0, 1, 1)}),
                capture(corrupt=True),
                capture()[:-56],
            ]
            for number, payload in enumerate(bad):
                proc = run(payload)
                self.assertNotEqual(proc.returncode, 0,
                                    f"Invalid trace {number} accepted")


if __name__ == "__main__":
    unittest.main()
