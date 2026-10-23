"""Stage30: the PTE failure trace must preserve exact address provenance."""
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

from test_stage24_usb import fake_capture as stage24_capture

HERE = Path(__file__).resolve().parent
VA = 0xFFFFFFF008900000
START_VA = VA
PHYS = 0x10200000
END_PHYS = PHYS + 0x2000
ROOT = 0x824C1000


def trace(*, omit=None, change=None):
    base = stage24_capture()
    extras = {
        0x2300: [(0x2500, (0x81405218, 0xFFFFFFF007405218, 0x941AE25E))],
        0x2407: [(0x2501, (0xF2000E25, 0xFFFFFFF007405218, 1)),
                 (0x2502, (VA, PHYS, END_PHYS)),
                 (0x2503, (START_VA, 1, 0x0)),
                 (0x2504, (ROOT + 8*((VA >> 30)&511), 0, 0)),
                 (0x2507, (VA, ROOT, 1))],
        0x220B: [(0x2508, (1, 0x81405218, 0x941AE25E))],
    }
    rows = []
    for off in range(0, len(base), 56):
        pkt = bytearray(base[off:off+56])
        ev = struct.unpack_from('<I', pkt, 16)[0]
        if ev == 0x2206:
            struct.pack_into('<QQQ', pkt, 28, 0xF2000E25, 0xFFFFFFF007405218, 0)
        elif ev == 0x220A:
            struct.pack_into('<QQQ', pkt, 28, 3, 0xFFFFFFF007405218, 0x3C)
        elif ev == 0x2404:
            struct.pack_into('<QQQ', pkt, 28, VA, START_VA, END_PHYS)
        elif ev == 0x2405:
            struct.pack_into('<QQQ', pkt, 28, PHYS, 1, 0)
        elif ev == 0x2407:
            struct.pack_into('<Q', pkt, 44, 30)
        if ev == 0x220B:
            # stage20b_restore emits its pmap restoration proof just
            # before the ordinary Stage22 vector-restoration marker.
            for child, vals in extras[ev]:
                ep = bytearray(pkt)
                struct.pack_into('<I', ep, 16, child)
                struct.pack_into('<QQQ', ep, 28, *vals)
                if child != omit:
                    rows.append((child, ep))
        rows.append((ev, pkt))
        if ev == 0x220B:
            continue
        for child, vals in extras.get(ev, ()):
            ep = bytearray(pkt)
            struct.pack_into('<I', ep, 16, child)
            struct.pack_into('<QQQ', ep, 28, *vals)
            if child != omit:
                rows.append((child, ep))

    result = bytearray()
    for i, (ev, pkt) in enumerate(rows):
        pkt[6] = 30
        struct.pack_into('<I', pkt, 8, i)
        struct.pack_into('<Q', pkt, 20, i*13)
        if change and ev == change[0]:
            struct.pack_into('<QQQ', pkt, 28, *change[1])
        struct.pack_into('<I', pkt, 52, zlib.crc32(pkt[:52]))
        result += pkt
    return bytes(result)


class Stage25USB(unittest.TestCase):
    def test_mapping_report_is_bound_to_pmap_registers_and_restoration(self):
        with tempfile.TemporaryDirectory(dir=HERE/'out/cactus') as folder:
            path = Path(folder)/'stage25.bin'
            cmd = [sys.executable, str(HERE/'decode-cactus-usb.py'),
                   str(path), '--require-stage25', '--show', '0']

            def run(raw):
                path.write_bytes(raw)
                return subprocess.run(cmd, text=True, capture_output=True)

            good = run(trace())
            self.assertEqual(good.returncode, 0, good.stdout + good.stderr)
            self.assertIn('NULL-PTE REQUEST CAPTURED; COM25 HELD', good.stdout)
            self.assertIn(f'failing_va={VA:#018x}', good.stdout)
            self.assertIn(f'phys_start={PHYS:#018x}', good.stdout)

            for bad in (trace(omit=0x2500), trace(omit=0x2501),
                        trace(omit=0x2502), trace(omit=0x2504),
                        trace(omit=0x2507), trace(omit=0x2508),
                        trace(change=(0x2502, (VA+0x1000, PHYS, END_PHYS))),
                        trace(change=(0x2502, (VA, PHYS, PHYS-0x1000))),
                        trace(change=(0x2508, (0, 0x81405218, 0x941AE25E))),
                        trace()[:-56]):
                proc = run(bad)
                self.assertNotEqual(proc.returncode, 0, proc.stdout)


if __name__ == '__main__':
    unittest.main()
