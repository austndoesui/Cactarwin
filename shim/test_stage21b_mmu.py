#!/usr/bin/env python3
"""Check the real Stage21-B binary and fail closed on changed CPU state."""
from pathlib import Path
import unittest

from check_stage21b_mmu import Payload, audit, TCR_PA

PAYLOAD = (Path(__file__).resolve().parent /
           "out/cactus/linux-chainload/stage18-payload.bin")


class MMUAudit(unittest.TestCase):
    def test_real_payload_and_changes(self):
        original = PAYLOAD.read_bytes()
        result = audit(original)
        self.assertEqual(result["translation"]["TG1"], 2)
        self.assertEqual(result["unmapped_top_bytes"], 0x200000)
        self.assertEqual(result["post_mmu_el1t_sync_slot_pa"], 0x81429000)
        p = Payload(original)
        for pa in (TCR_PA, 0x8142F18C, 0x8142F3CC, 0x8142F488,
                   0x81429000, 0x81433F68):
            # Find exact backing offset, not another identical opcode.
            va = p.virt + pa - p.phys
            offset = next(f + va - segva for segva, _, f, n in p.segs
                          if segva <= va and va + 4 <= segva + n)
            changed = bytearray(original)
            changed[offset] ^= 1
            with self.assertRaises(ValueError):
                audit(changed)


if __name__ == "__main__":
    unittest.main()
