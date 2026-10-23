#!/usr/bin/env python3
"""Stage24 rejects altered EL1t vector, target, and MMU code."""
import struct
import unittest
from pathlib import Path

from check_stage21b_mmu import Payload
from check_stage21c1_entry import inspect, TARGET, VECTOR

PAYLOAD = (Path(__file__).resolve().parent /
           "out/cactus/linux-chainload/stage18-payload.bin")


class Entry(unittest.TestCase):
    def test_guard_and_mutations(self):
        original = PAYLOAD.read_bytes()
        self.assertEqual(inspect(original), (TARGET, VECTOR))
        p = Payload(original)
        for pa in (TARGET, VECTOR, VECTOR+4, 0x8142F18C,
                   0x8142F3CC, 0x8142F488):
            va = p.virt + pa - p.phys
            off = next(fileoff + va - segva
                       for segva, _, fileoff, size in p.segs
                       if segva <= va and va+4 <= segva+size)
            modified = bytearray(original)
            modified[off] ^= 1
            with self.assertRaises(ValueError, msg=f"changed {pa:#x}"):
                inspect(modified)


if __name__ == "__main__":
    unittest.main()
