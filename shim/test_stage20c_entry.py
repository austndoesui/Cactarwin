#!/usr/bin/env python3
"""Real relocated-payload / fail-closed opcode regression for Stage20-C."""
from pathlib import Path
import struct
import unittest

from check_stage20c_entry import inspect

PAYLOAD = Path(__file__).resolve().parent / "out/cactus/linux-chainload/stage18-payload.bin"


class EntryTests(unittest.TestCase):
    def test_real_code_and_corruptions(self):
        blob = PAYLOAD.read_bytes()
        self.assertEqual(inspect(blob)[:2], (0x8142f0ec, 0x8142f144))
        base, _ = struct.unpack_from("<QQ", blob, 16)
        phys = struct.unpack_from("<Q", blob, 56)[0]
        va = base + 0x8142f0ec - phys
        segs = [struct.unpack_from("<4Q", blob, 0x2a8+32*i) for i in range(10)]
        offset = next(f+va-v for v, _, f, n in segs if v <= va < v+n)
        for at in (offset, offset+4, offset+0x34, offset+0x58):
            damaged = bytearray(blob)
            damaged[at] ^= 1
            with self.assertRaises(ValueError):
                inspect(damaged)


if __name__ == "__main__":
    unittest.main()
