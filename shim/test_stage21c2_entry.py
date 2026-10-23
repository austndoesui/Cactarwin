#!/usr/bin/env python3
"""Mutation checks for the new post-MMU instruction and vector guard."""
import unittest
from pathlib import Path

from check_stage21b_mmu import Payload
from check_stage21c2_entry import inspect, TARGET, VECTOR, VBAR_VA

PAYLOAD = (Path(__file__).resolve().parent /
           "out/cactus/linux-chainload/stage18-payload.bin")


class Entry(unittest.TestCase):
    def test_post_mmu_guard_and_mutations(self):
        original = PAYLOAD.read_bytes()
        self.assertEqual(inspect(original), (TARGET, VECTOR, VBAR_VA))
        p = Payload(original)
        for pa in (TARGET, 0x8142F488, 0x8142F48C,
                   VECTOR, VECTOR + 4):
            va = p.virt + pa - p.phys
            offset = next(off + va - start for start, _, off, size in p.segs
                          if start <= va and va + 4 <= start + size)
            modified = bytearray(original)
            modified[offset] ^= 1
            with self.assertRaises(ValueError, msg=f"altered {pa:#x}"):
                inspect(modified)


if __name__ == "__main__":
    unittest.main()
