#!/usr/bin/env python3
"""Stage28: distinguish real synchronous fault from handoff-only evidence."""
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

from test_stage22_usb import capture as stage22_capture

HERE = Path(__file__).resolve().parent

ADDITIONS = {
    0x2204: [(0x2300, (0x81AC0C0C, 0xB0FFACC1, 0xB0FFACC1))],
    0x2205: [
        (0x2301, (0x5E605000 + 80*2880 + 48*4,
                  0x00FF00FF0000FF00, 0x0000FFFF)),
        (0x2302, ((48 << 32) | 80, (288 << 32) | 80, 1)),
        (0x2303, (0x8142D488, 0x824BC000, 0xFFFFFFF007AC0C0C)),
    ],
    0x220B: [(0x2308, (0, 1, 1))],
}


def captured(*, no_fault=False, changes=None, corrupt=False):
    original = stage22_capture(fault=True)
    rows = []
    for off in range(0, len(original), 56):
        packet = original[off:off+56]
        event = struct.unpack_from("<I", packet, 16)[0]
        if no_fault and event in (0x2206, 0x2207, 0x2208, 0x2209,
                                  0x220A, 0x20B3, 0x20A3, 0x220B,
                                  0x20FF, 0x19FF):
            continue
        values = struct.unpack_from("<QQQ", packet, 28)
        rows.append((event, packet[7], values))
        if event in ADDITIONS:
            for extra, payload in ADDITIONS[event]:
                rows.append((extra, 8, payload))
    changes = changes or {}
    data = bytearray()
    for index, (event, el, values) in enumerate(rows):
        values = changes.get(event, values)
        pkt = bytearray(original[:56])
        pkt[6] = 28
        pkt[7] = el
        struct.pack_into("<I", pkt, 8, index)
        struct.pack_into("<I", pkt, 16, event)
        struct.pack_into("<Q", pkt, 20, index*13)
        struct.pack_into("<QQQ", pkt, 28, *values)
        struct.pack_into("<I", pkt, 52, zlib.crc32(pkt[:52]))
        if corrupt and event == 0x2301:
            pkt[53] ^= 0x08
        data += pkt
    return bytes(data)


class Stage23USB(unittest.TestCase):
    def test_genuine_fault_held_and_unconfirmed_handoff(self):
        with tempfile.TemporaryDirectory(dir=HERE / "out/cactus") as folder:
            path = Path(folder) / "trace.bin"
            cmd = [sys.executable, str(HERE / "decode-cactus-usb.py"),
                   str(path), "--require-stage23", "--show", "0"]

            def run(data):
                path.write_bytes(data)
                return subprocess.run(cmd, text=True, capture_output=True)

            good = run(captured())
            self.assertEqual(good.returncode, 0, good.stdout + good.stderr)
            self.assertIn("GENUINE XNU FAULT CAPTURED; COM25 HELD", good.stdout)

            partial = run(captured(no_fault=True))
            self.assertEqual(partial.returncode, 2, partial.stdout + partial.stderr)
            self.assertIn("XNU OUTCOME UNKNOWN", partial.stdout)

            variants = [
                captured(changes={0x2300: (0x81AC0C0C, 0xB0FFACC1, 0xD421C480)}),
                captured(changes={0x2301: (0, 0, 0)}),
                captured(changes={0x2302: ((48 << 32) | 80, (288 << 32) | 80, 0)}),
                captured(changes={0x220A: (1, 0, 0x25)}),
                captured(changes={0x220B: (0, 1, 1)}),
                captured(changes={0x2308: (0, 0, 0)}),
                captured(corrupt=True),
                captured()[:-56],
            ]
            for n, bad in enumerate(variants):
                proc = run(bad)
                self.assertNotEqual(proc.returncode, 0,
                                    f"invalid trace {n} accepted")


if __name__ == "__main__":
    unittest.main()
