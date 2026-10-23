"""Stage29: the logger must retain original XNU context and reject loss."""
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

from test_stage23_usb import captured as stage23_capture

HERE = Path(__file__).resolve().parent
REGS = {
    0x2400: (0xFFFFFFF00FFF0000, 0xFFFFFFF0072A9E68, 0xFFFFFFF00FFF0000),
    0x2401: (11, 12, 13),
    0x2402: (21, 22, 23),
    0x2403: (31, 32, 33),
    0x2404: (41, 42, 43),
    0x2405: (51, 52, 53),
    0x2406: (61, 62, 63),
    0x2407: (71, 0xFFFFFFF00FFF0100, 0x29),
    0x241f: (0xFFFFFFF00FFF0000, 0x824C1000, 0),
}


def fake_capture(*, omit=None, change=None, move=False):
    source = stage23_capture()
    rows = []
    for offset in range(0, len(source), 56):
        packet = bytearray(source[offset:offset+56])
        event = struct.unpack_from('<I', packet, 16)[0]
        if event == 0x2206:
            struct.pack_into('<QQQ', packet, 28,
                             0x02000000, 0xFFFFFFF0072A9E64, 0)
        if event == 0x220A:
            struct.pack_into('<QQQ', packet, 28,
                             2, 0xFFFFFFF0072A9E64, 0)
        if event == 0x2208:
            struct.pack_into('<QQQ', packet, 28,
                             0x824C0000, 0x824C1000, 0x0C08044F00BB44FF)
        rows.append((event, packet))
        if event == (0x2207 if move else 0x2206):
            for ev, values in REGS.items():
                if ev == omit:
                    continue
                extra = bytearray(packet)
                struct.pack_into('<I', extra, 16, ev)
                struct.pack_into('<QQQ', extra, 28, *(change if ev == 0x2407
                                                     and change else values))
                rows.append((ev, extra))

    data = bytearray()
    for i, (_, pkt) in enumerate(rows):
        pkt[6] = 29
        struct.pack_into('<I', pkt, 8, i)
        struct.pack_into('<Q', pkt, 20, i * 13)
        struct.pack_into('<I', pkt, 52, zlib.crc32(pkt[:52]))
        data += pkt
    return bytes(data)


class Stage24USB(unittest.TestCase):
    def test_full_register_snapshot_and_missing_or_reordered_context(self):
        with tempfile.TemporaryDirectory(dir=HERE / 'out/cactus') as folder:
            path = Path(folder)/'stage24.bin'
            cmd = [sys.executable, str(HERE/'decode-cactus-usb.py'),
                   str(path), '--require-stage24', '--show', '0']

            def check(raw):
                path.write_bytes(raw)
                return subprocess.run(cmd, text=True, capture_output=True)

            good = check(fake_capture())
            self.assertEqual(good.returncode, 0, good.stdout+good.stderr)
            self.assertIn('STAGE24 GENUINE XNU FAULT CAPTURED', good.stdout)

            for bad in (fake_capture(omit=0x2400),
                        fake_capture(omit=0x2407),
                        fake_capture(omit=0x241f),
                        fake_capture(change=(71, 0, 0)),
                        fake_capture(move=True),
                        fake_capture()[:-56]):
                result = check(bad)
                self.assertNotEqual(result.returncode, 0,
                                    'incorrect Stage29 context was accepted')


if __name__ == '__main__':
    unittest.main()
