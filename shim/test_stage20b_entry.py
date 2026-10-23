#!/usr/bin/env python3
"""Regression for the Stage20-B first-instruction admission gate."""
from importlib.machinery import SourceFileLoader
from pathlib import Path
import struct
import unittest

ROOT = Path(__file__).resolve().parent
checker = SourceFileLoader(
    "stage20b_entry", str(ROOT / "check_stage20b_entry.py")
).load_module()


class EntryTests(unittest.TestCase):
    def test_real_payload_and_mutations(self):
        data = (ROOT / "out/cactus/linux-chainload/stage18-payload.bin").read_bytes()
        self.assertEqual(checker.inspect(data), (
            0x8142d488, 0x8142f0ec, 0x14000719, 0xd510109f))
        entry = struct.unpack_from("<Q", data, 24)[0]
        segments = [struct.unpack_from("<4Q", data, 0x2a8 + i * 32)
                    for i in range(10)]
        offset = next(fileoff + entry - vmaddr
                      for vmaddr, _, fileoff, size in segments
                      if vmaddr <= entry < vmaddr + size)
        for at in (offset, offset + 0x1c64):
            mutated = bytearray(data)
            mutated[at] ^= 1
            with self.assertRaises(ValueError):
                checker.inspect(mutated)


if __name__ == "__main__":
    unittest.main()
