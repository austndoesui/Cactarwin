#!/usr/bin/env python3
"""Stage26 strict decoder: checkpoint, genuine fault, and corrupted captures."""
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

from test_cactus_usb_decode import frame

ROOT = Path(__file__).resolve().parent


def capture(*, fault=False, change=None, missing=None, corrupt=False):
    events = [0x1900, 0x1901, 0x1910, 0x1912, 0x1911, 0x1930]
    for n in range(10):
        events.extend((0x1940, 0x1941))
    events.extend((0x1950, 0x1951, 0x1952, 0x21e6, 0x21e0,
                   0x20a0, 0x21ea, 0x20a1, 0x21e1, 0x21e2,
                   0x21e3, 0x21e4, 0x21e5, 0x20b3, 0x20a3,
                   0x21e8, 0x20ff, 0x19ff))
    if missing:
        events.remove(missing)
    target = 0xfffffff007ac061c
    esr = 0x96000004 if fault else 0xf2000e23
    elr = 0xfffffff007ac0608 if fault else target
    values = {
        0x1901: (1, 0, 0), 0x1910: (1, 0, 0), 0x1911: (0, 0, 0),
        0x21e6: (0x81429000, 0x81429200, 0xfffffff007429000),
        0x21e0: (0x8142d488, 0x81ac061c, 0x14000719),
        0x21ea: (0x81ac061c, target, 0),
        0x20a1: (4, 0x8142d488, 0x824bc000),
        0x21e1: (esr, elr, 0xfffffff007f00000),
        0x21e2: (0xfffffff007429000, 0x22a5192519, 0x34d4593d),
        0x21e3: (0x8117a000, 0x8117b000, 0x0c08044f00bb44ff),
        0x21e4: (0xfffffff007429004, 0x3c5, 0x3c4),
        0x21e5: (2 if fault else 1, elr, 0x25 if fault else 0x3c),
        0x20b3: (0x5a000021, 0xfffffff007429004, 0x3c5),
        0x20a3: (8, 0, 0), 0x21e8: (1, 1, 1),
        0x20ff: (0, 0, 0), 0x19ff: (0, 0, 0),
    }
    chunks = []
    seg = 0
    for i, event in enumerate(events):
        payload = bytearray(frame(i, event, stage=26))
        v = values.get(event)
        if event in (0x1940, 0x1941):
            v = (seg, 0, 0)
            if event == 0x1941:
                seg += 1
        if change and event == change[0]:
            v = change[1]
        if v:
            struct.pack_into("<QQQ", payload, 28, *v)
        if event == 0x20a1:
            payload[7] = 4
        struct.pack_into("<I", payload, 52, zlib.crc32(payload[:52]))
        if corrupt and event == 0x21e1:
            payload[52] ^= 1
        chunks.append(bytes(payload))
    return b"".join(chunks)


class USB(unittest.TestCase):
    def test_checkpoint_fault_and_mutations(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "out/cactus") as folder:
            target = Path(folder) / "stage26.bin"
            args = [sys.executable, str(ROOT / "decode-cactus-usb.py"),
                    str(target), "--require-stage21d", "--show", "0"]

            def evaluate(blob):
                target.write_bytes(blob)
                return subprocess.run(args, text=True, capture_output=True)

            for fault, expected in (
                    (False, "HIGH-VA C CHECKPOINT PASS"),
                    (True, "EARLY XNU EXCEPTION CAPTURED")):
                result = evaluate(capture(fault=fault))
                self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
                self.assertIn(expected, result.stdout)
            invalid = [
                capture(missing=0x21e1),
                capture(missing=0x21e6),
                capture(change=(0x21e6, (0x81429000, 0x81429200, 0))),
                capture(change=(0x21e1, (0xf2000e22, 0xfffffff007ac061c, 0))),
                capture(change=(0x21e2, (0xfffffff007429000, 0x22a5192519, 0x34d4593c))),
                capture(change=(0x21e5, (1, 0xfffffff007ac0618, 0x3c))),
                capture(change=(0x21e8, (0, 1, 1))),
                capture(corrupt=True),
                capture()[:-56],
            ]
            for n, blob in enumerate(invalid):
                self.assertNotEqual(evaluate(blob).returncode, 0,
                                    f"corrupt trace {n} passed")


if __name__ == "__main__":
    unittest.main()
