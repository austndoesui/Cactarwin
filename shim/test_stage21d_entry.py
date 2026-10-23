#!/usr/bin/env python3
"""Corrupt representative C prologue/vector bytes to test the Stage26 guard."""
import unittest
from pathlib import Path

from check_stage21b_mmu import Payload
from check_stage21d_entry import inspect, ENTRY, TARGET, TARGET_VA, EL1H_VECTOR

PAYLOAD = (Path(__file__).resolve().parent /
           "out/cactus/linux-chainload/stage18-payload.bin")


class HighVA(unittest.TestCase):
    def test_prologue_and_vector_guard(self):
        data = PAYLOAD.read_bytes()
        self.assertEqual(inspect(data), (TARGET, TARGET_VA))
        p = Payload(data)
        for pa in (ENTRY, ENTRY+16, ENTRY+60, TARGET,
                   EL1H_VECTOR, EL1H_VECTOR+4):
            va = p.virt + pa - p.phys
            position = next(off + va - start for start, _, off, size in p.segs
                            if start <= va and va + 4 <= start + size)
            corrupted = bytearray(data)
            corrupted[position] ^= 1
            with self.assertRaises(ValueError, msg=f"{pa:#x} changed"):
                inspect(corrupted)


if __name__ == "__main__":
    unittest.main()
