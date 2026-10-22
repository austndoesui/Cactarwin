#!/usr/bin/env python3
"""Host test of Benesse-derived FDT range checks on the real cactus DTB."""
import ctypes
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent
DTB = Path("/home/lapdq/cactus-kernel/pack/cactus_dtb.orig.raw")


class Range(ctypes.Structure):
    _fields_ = [("base", ctypes.c_uint64), ("size", ctypes.c_uint64)]


class Info(ctypes.Structure):
    _fields_ = [
        ("bytes", ctypes.c_uint32),
        ("memory_count", ctypes.c_uint),
        ("reserved_count", ctypes.c_uint),
        ("memory", Range * 8),
        ("reserved", Range * 64),
        ("initrd_start", ctypes.c_uint64),
        ("initrd_end", ctypes.c_uint64),
    ]


with tempfile.TemporaryDirectory() as temp:
    binary = str(Path(temp) / "fdt_ranges.so")
    subprocess.run(["gcc", "-Wall", "-Wextra", "-Werror", "-shared",
                    "-fPIC", "-O2", str(ROOT / "fdt_ranges.c"),
                    "-o", binary], check=True)
    lib = ctypes.CDLL(binary)
    lib.fdt_inspect.argtypes = [
        ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(Info)
    ]
    lib.fdt_inspect.restype = ctypes.c_int
    lib.fdt_region_free.argtypes = [
        ctypes.POINTER(Info), ctypes.c_uint64, ctypes.c_uint64
    ]
    lib.fdt_region_free.restype = ctypes.c_int
    blob = DTB.read_bytes()
    info = Info()
    result = lib.fdt_inspect(ctypes.create_string_buffer(blob), len(blob),
                             ctypes.byref(info))
    print(f"cactus FDT: status={result}, memory={info.memory_count}, "
          f"reserved={info.reserved_count}, bytes={info.bytes}")
    assert result == 0, "live cactus DTB not accepted by bounded parser"
    for r in info.memory[:info.memory_count]:
        print(f"  RAM {r.base:#x} + {r.size:#x}")
    for r in info.reserved[:info.reserved_count]:
        print(f"  RESERVED {r.base:#x} + {r.size:#x}")
    assert info.memory_count
    assert lib.fdt_region_free(ctypes.byref(info), 0xfffffffffffffff0, 256) == 0
    if info.reserved_count:
        r = info.reserved[0]
        assert lib.fdt_region_free(ctypes.byref(info), r.base, r.size) == 0
    if lib.fdt_region_free(ctypes.byref(info), 0x70000000, 0x50000000) != 1:
        print("INFO: XNU configured RAM [0x70000000,0xc0000000) is "
              "not fully free in live DTB; Stage15 must reject this payload.")
    print("Stage15 FDT parser and range checks PASS")
