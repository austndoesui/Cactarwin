"""Stage30 rejects any drift in the verified pmap null-PTE call site."""
import unittest
from pathlib import Path

from check_stage21b_mmu import Payload
from check_stage25_pmap import inspect, PINS

PAYLOAD = Path(__file__).resolve().parent / 'out/cactus/linux-chainload/stage18-payload.bin'


class Stage25Entry(unittest.TestCase):
    def test_guard_rejects_mutated_reg_prologue_and_panic_join(self):
        original = PAYLOAD.read_bytes()
        self.assertEqual(inspect(original), (0x81405218, 0xFFFFFFF007405218))
        p = Payload(original)
        for pa in PINS:
            va = p.virt + pa - p.phys
            index = next(offset + va - base for base, _, offset, length in p.segs
                         if base <= va < base + length - 3)
            changed = bytearray(original)
            changed[index] ^= 1
            with self.assertRaises(ValueError, msg=f'{pa:#x}'):
                inspect(changed)


if __name__ == '__main__':
    unittest.main()
