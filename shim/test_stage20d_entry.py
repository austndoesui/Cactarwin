#!/usr/bin/env python3
"""Verify exact XNU vector code and fail-closed mutation detection."""
from pathlib import Path
import struct
import unittest
from check_stage20d_entry import inspect

PAYLOAD = Path(__file__).resolve().parent / "out/cactus/linux-chainload/stage18-payload.bin"


class Gate(unittest.TestCase):
    def test_actual_payload_and_mutations(self):
        data = PAYLOAD.read_bytes()
        self.assertEqual(inspect(data), (0x8142E000, 0x8142E200, 0x8142F154))
        va_base = struct.unpack_from("<Q", data, 16)[0]
        pa_base = struct.unpack_from("<Q", data, 56)[0]
        segments = [struct.unpack_from("<4Q", data, 0x2a8+32*i)
                    for i in range(10)]
        for pa in (0x8142e200, 0x8142e204, 0x8142f148, 0x8142f154):
            va = va_base + pa - pa_base
            file_offset = next(f+va-v for v, _, f, n in segments
                               if v <= va < v+n)
            altered = bytearray(data)
            altered[file_offset] ^= 1
            with self.assertRaises(ValueError):
                inspect(altered)


if __name__ == "__main__":
    unittest.main()
