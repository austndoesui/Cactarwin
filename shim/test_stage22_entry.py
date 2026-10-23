#!/usr/bin/env python3
"""Reject mutated XNU instructions along the Stage22 startup path."""
import unittest
from pathlib import Path

from check_stage21b_mmu import Payload
from check_stage22_entry import inspect, PINS, TARGET, TARGET_VA

PAYLOAD = (Path(__file__).resolve().parent /
           "out/cactus/linux-chainload/stage18-payload.bin")


class Stage22Payload(unittest.TestCase):
    def test_early_calls_and_late_checkpoint_are_pinned(self):
        data = PAYLOAD.read_bytes()
        self.assertEqual(inspect(data), (TARGET, TARGET_VA))
        p = Payload(data)
        # Exercise both early control-flow joins and the last C call.
        for pa in (0x81AC0624, 0x81AC0668, 0x81AC06EC,
                   0x81AC0BA0, 0x81AC0C08, TARGET):
            self.assertEqual(p.word(pa), PINS[pa])
            va = (p.virt + pa - p.phys) & ((1 << 64) - 1)
            offset = next(start_off + va - seg_va
                          for seg_va, _, start_off, size in p.segs
                          if seg_va <= va and va + 4 <= seg_va + size)
            corrupted = bytearray(data)
            corrupted[offset] ^= 1
            with self.assertRaises(ValueError, msg=f"{pa:#x} mutation"):
                inspect(corrupted)


if __name__ == "__main__":
    unittest.main()
