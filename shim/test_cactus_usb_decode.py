import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

from importlib.machinery import SourceFileLoader
from pathlib import Path

decoder = SourceFileLoader("cactus_usb_decode",
                           str(Path(__file__).with_name("decode-cactus-usb.py"))).load_module()


def frame(seq, event=0x1403, stage=14, level=0, result=None):
    body = struct.pack("<4sBBBBIIIQQQQ", b"CLOG", 2, 56, stage, 8,
                       seq, 0xca140001, event, seq * 13,
                       (result if result is not None else
                        level if event == 0x158a else seq),
                       0x8040201000ff00aa ^ seq,
                       0 if seq % 2 == 0 else 0xffffffffffffffff)
    assert len(body) == 52
    return body + struct.pack("<I", zlib.crc32(body))


class DecoderTests(unittest.TestCase):
    def test_stage20b_first_real_branch_requires_exact_trap(self):
        events = [0x1900, 0x1901, 0x1910, 0x1912, 0x1911, 0x1930]
        for _ in range(10):
            events.extend((0x1940, 0x1941))
        events.extend((0x1950, 0x1951, 0x1952, 0x20b0, 0x20a0,
                       0x20a1, 0x20b2, 0x20b3, 0x20a3, 0x20b4,
                       0x20ff, 0x19ff))

        def data(bad_trap=False, wrong_target=False):
            built = []
            segment = 0
            for index, event in enumerate(events):
                f = bytearray(frame(index, event, stage=21))
                if event in (0x1901, 0x1910):
                    struct.pack_into("<Q", f, 28, 1)
                if event in (0x1911, 0x19ff, 0x20ff):
                    struct.pack_into("<Q", f, 28, 0)
                if event in (0x1940, 0x1941):
                    struct.pack_into("<Q", f, 28, segment)
                    if event == 0x1941:
                        segment += 1
                if event == 0x20b0:
                    struct.pack_into("<QQQ", f, 28, 0x8142d488,
                                     0x8142f0ec, 0x14000719)
                if event == 0x20a1:
                    f[7] = 4
                    struct.pack_into("<QQQ", f, 28, 4, 0x8142d488,
                                     0x824bc000)
                if event == 0x20b2:
                    f[7] = 4
                    struct.pack_into("<QQQ", f, 28,
                                     (0x3c << 26) | (0xb21 if bad_trap else 0xb20),
                                     0x8142f0ec + (4 if wrong_target else 0), 0)
                if event == 0x20b3:
                    struct.pack_into("<QQQ", f, 28, (0x16 << 26) | 0x21,
                                     0x41e38060, 0x3c5)
                if event == 0x20a3:
                    struct.pack_into("<Q", f, 28, 8)
                if event == 0x20b4:
                    struct.pack_into("<QQQ", f, 28, 1, 1, 1)
                struct.pack_into("<I", f, 52, zlib.crc32(f[:52]))
                built.append(bytes(f))
            return b"".join(built)

        with tempfile.TemporaryDirectory(
                dir=Path(__file__).resolve().parent / "out" / "cactus") as folder:
            capture = Path(folder) / "stage20b.bin"
            command = [sys.executable,
                       str(Path(__file__).with_name("decode-cactus-usb.py")),
                       str(capture), "--require-stage20b"]
            capture.write_bytes(data())
            good = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(good.returncode, 0, good.stdout + good.stderr)
            self.assertIn("FIRST REAL XNU BRANCH PASS", good.stdout)
            for bad in (data(bad_trap=True), data(wrong_target=True),
                        data()[:-56]):
                capture.write_bytes(bad)
                failure = subprocess.run(command, capture_output=True, text=True)
                self.assertNotEqual(failure.returncode, 0)

    def test_variable_packets_and_unaligned_capture(self):
        data = b"noise" + b"".join(frame(i) for i in range(120)) + b"tail"
        items, bad, noise = decoder.decode(data)
        self.assertEqual(len(items), 120)
        self.assertEqual((bad, noise), (0, 9))
        self.assertEqual(decoder.assess(items), (0, 0, 0))
        self.assertNotEqual(items[0]["b"], items[-1]["b"])

    def test_crc_resync_and_sequence_gaps(self):
        broken = bytearray(frame(2))
        broken[30] ^= 0x1
        data = frame(0) + bytes(broken) + frame(3)
        items, bad, noise = decoder.decode(data)
        self.assertEqual(len(items), 2)
        self.assertEqual(bad, 1)
        self.assertEqual(decoder.assess(items), (2, 0, 0))

    def test_truncated_packet_and_duplicate(self):
        items, bad, noise = decoder.decode(frame(0) + frame(0) + frame(5)[:10])
        self.assertEqual((len(items), bad, noise), (2, 0, 10))
        self.assertEqual(decoder.assess(items), (0, 1, 0))

    def test_early_stage15_gate_requires_real_milestones(self):
        events = [0x1580, 0x1581] + [0x1582] * 19 + [0x158a]
        capture = b"".join(frame(i, event, 15, 1)
                           for i, event in enumerate(events))
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parent / "out" / "cactus") as directory:
            file = Path(directory) / "early.bin"
            file.write_bytes(capture)
            command = [sys.executable, str(Path(__file__).with_name(
                "decode-cactus-usb.py")), str(file),
                       "--require-stage15-early", "--early-level", "1"]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("STAGE15 EARLY PASS", result.stdout)
            result = subprocess.run(command[:-1] + ["2"],
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("NOT VERIFIED", result.stdout)

    def test_stage16_gate_checks_order_and_fdt_result(self):
        events = [0x1600, 0x1601, 0x1602, 0x1610, 0x1611, 0x1612,
                  0x1613, 0x1616, 0x1620, 0x1621, 0x16ff]
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parent / "out" / "cactus") as directory:
            file = Path(directory) / "stage16.bin"
            command = [sys.executable, str(Path(__file__).with_name(
                "decode-cactus-usb.py")), str(file), "--require-stage16"]
            file.write_bytes(b"".join(frame(i, event, stage=16,
                              result=1 if event in (0x1601, 0x1612) else None)
                              for i, event in enumerate(events)))
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("STAGE16 PREFLIGHT PASS", result.stdout)
            file.write_bytes(b"".join(frame(i, event, stage=16,
                              result=1 if event == 0x1601 else 0 if event == 0x1612 else None)
                              for i, event in enumerate(events)))
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("fdt_parsed=False", result.stdout)

    def test_stage17_map_gate_requires_actual_bank_and_completion(self):
        events = [0x1700, 0x1701, 0x1710, 0x1711, 0x1712,
                  0x1713, 0x1714, 0x1715, 0x1718, 0x1702, 0x17ff]
        directory = Path(__file__).resolve().parent / "out" / "cactus"
        with tempfile.TemporaryDirectory(dir=directory) as temp:
            file = Path(temp) / "stage17.bin"
            command = [sys.executable, str(Path(__file__).with_name(
                "decode-cactus-usb.py")), str(file), "--require-stage17"]
            file.write_bytes(b"".join(frame(i, event, stage=17,
                result=1 if event in (0x1701, 0x1711, 0x1712) else None)
                for i, event in enumerate(events)))
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("STAGE17 MAP AND COMPLETION PASS", result.stdout)
            file.write_bytes(b"".join(frame(i, event, stage=17,
                result=1 if event in (0x1701, 0x1711, 0x1712) else None)
                for i, event in enumerate(events) if event != 0x1714))
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("0x1714", result.stdout)

    def test_stage18_admission_requires_zero_verdict_all_segments_and_order(self):
        events = [0x1800, 0x1801, 0x1810, 0x1811, 0x1820, 0x1821,
                  0x1830, 0x1831, 0x1840, 0x1840, 0x18f0, 0x18ff]
        def record(i, event, verdict=0):
            data = bytearray(frame(i, event, stage=18, result=
                              (1 if event in (0x1801, 0x1811, 0x1821) else
                               verdict if event in (0x1831, 0x18ff) else
                               0x80000000 if event == 0x18f0 else None)))
            if event == 0x1820:
                struct.pack_into("<Q", data, 44, 2)
            if event == 0x18f0:
                struct.pack_into("<Q", data, 36, 0x82000000)
            struct.pack_into("<I", data, 52, zlib.crc32(data[:52]))
            return bytes(data)
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parent /
                                         "out" / "cactus") as directory:
            file = Path(directory) / "stage18.bin"
            command = [sys.executable, str(Path(__file__).with_name(
                "decode-cactus-usb.py")), str(file), "--require-stage18"]
            file.write_bytes(b"".join(record(i, e) for i, e in enumerate(events)))
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("STAGE18 PAYLOAD ADMISSION PASS", result.stdout)
            file.write_bytes(b"".join(record(i, e, verdict=4)
                               for i, e in enumerate(events)))
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("verdicts=[4]", result.stdout)
            file.write_bytes(b"".join(record(i, e) for i, e in
                               enumerate(events) if i != 9))
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)

    def test_stage20_gate_proves_el1_and_hvc_return(self):
        events = [0x1900, 0x1901, 0x1910, 0x1912, 0x1911, 0x1930]
        for i in range(10):
            events.extend([0x1940, 0x1941])
        events.extend([0x1950, 0x1951, 0x1952, 0x20a0,
                       0x20a1, 0x20a2, 0x20a3, 0x20ff, 0x19ff])
        def make(event, seq, segment_no, wrong_el=False):
            b = bytearray(frame(seq, event, stage=20))
            if event in (0x1901, 0x1910):
                struct.pack_into("<Q", b, 28, 1)
            if event in (0x1911, 0x19ff, 0x20ff):
                struct.pack_into("<Q", b, 28, 0)
            if event in (0x1940, 0x1941):
                struct.pack_into("<Q", b, 28, segment_no)
            if event == 0x20a1:
                b[7] = 8 if wrong_el else 4
                struct.pack_into("<QQQ", b, 28, 4, 0x8142d488,
                                 0x824bc000)
            if event == 0x20a2:
                struct.pack_into("<QQQ", b, 28, (0x16 << 26) | 0x20,
                                 0x41e38060, 0x3c5)
            if event == 0x20a3:
                struct.pack_into("<Q", b, 28, 8)
            struct.pack_into("<I", b, 52, zlib.crc32(b[:52]))
            return bytes(b)
        def assemble(wrong_el=False, omit_finish=False):
            number = 0
            index = 0
            out = []
            for e in events:
                if e in (0x1940, 0x1941):
                    if omit_finish and e == 0x1941 and index == 5:
                        index += 1
                        continue
                    out.append(make(e, number, index // 2, wrong_el))
                    index += 1
                else:
                    out.append(make(e, number, 0, wrong_el))
                number += 1
            return b"".join(out)
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parent /
                                         "out" / "cactus") as folder:
            sample = Path(folder) / "stage20.bin"
            call = [sys.executable,
                    str(Path(__file__).with_name("decode-cactus-usb.py")),
                    str(sample), "--require-stage20"]
            sample.write_bytes(assemble())
            ok = subprocess.run(call, capture_output=True, text=True)
            self.assertEqual(ok.returncode, 0, ok.stdout + ok.stderr)
            self.assertIn("STAGE20 EL1 ERET/HVC RETURN PASS", ok.stdout)
            sample.write_bytes(assemble(wrong_el=True))
            bad = subprocess.run(call, capture_output=True, text=True)
            self.assertNotEqual(bad.returncode, 0)
            sample.write_bytes(assemble(omit_finish=True))
            missing = subprocess.run(call, capture_output=True, text=True)
            self.assertNotEqual(missing.returncode, 0)


if __name__ == "__main__":
    unittest.main()
