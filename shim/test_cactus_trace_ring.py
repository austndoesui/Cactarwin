"""Cross-check the actual freestanding C ring encoder against Python decoder."""
import ctypes
from importlib.machinery import SourceFileLoader
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent
decoder = SourceFileLoader("trace_decode",
                           str(ROOT / "decode-cactus-usb.py")).load_module()


class RingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        binary = str(Path(cls.temp.name) / "libtrace.so")
        subprocess.run(["cc", "-fPIC", "-shared", "-O2", "-Wall", "-Wextra",
                        "-Werror", "-DCACTUS_TRACE_HOST_TEST",
                        str(ROOT / "cactus_trace.c"), "-o", binary], check=True)
        cls.lib = ctypes.CDLL(binary)
        cls.lib.cactus_trace_reset.argtypes = [ctypes.c_uint32]
        cls.lib.cactus_trace_emit.argtypes = [ctypes.c_uint32,
                                              ctypes.c_uint64,
                                              ctypes.c_uint64,
                                              ctypes.c_uint64]
        cls.lib.cactus_trace_emit.restype = ctypes.c_int
        cls.lib.cactus_trace_peek.restype = ctypes.c_void_p
        cls.lib.cactus_trace_dropped.restype = ctypes.c_uint32
        cls.lib.cactus_trace_pending.restype = ctypes.c_uint32

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_real_encoder_wrap_and_checksum(self):
        lib = self.lib
        lib.cactus_trace_reset(0xca140001)
        for i in range(32):
            self.assertEqual(lib.cactus_trace_emit(0x1403, i, i << 34, ~i & ((1 << 64)-1)), 1)
        self.assertEqual(lib.cactus_trace_emit(0x1403, 33, 0, 0), 0)
        self.assertEqual(lib.cactus_trace_dropped(), 1)
        frames = bytearray()
        for _ in range(12):
            frames.extend(ctypes.string_at(lib.cactus_trace_peek(), 56))
            lib.cactus_trace_pop()
        for i in range(12):
            self.assertEqual(lib.cactus_trace_emit(0x1403, 44+i, 0, i), 1)
        while lib.cactus_trace_pending():
            frames.extend(ctypes.string_at(lib.cactus_trace_peek(), 56))
            lib.cactus_trace_pop()
        records, bad, noise = decoder.decode(bytes(frames))
        self.assertEqual((len(records), bad, noise), (44, 0, 0))
        self.assertEqual(decoder.assess(records), (1, 0, 0))
        self.assertEqual(records[0]["a"], 0)
        self.assertEqual(records[31]["a"], 31)
        self.assertEqual(records[-1]["a"], 55)


if __name__ == "__main__":
    unittest.main()
