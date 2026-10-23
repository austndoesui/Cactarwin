#!/usr/bin/env python3
"""Gate Stage20-C on the real early-XNU instruction sequence and framebuffer."""
import argparse
import hashlib
import struct
from pathlib import Path

from check_stage20b_entry import inspect as check_entry

EARLY_PA = 0x8142F0EC
BRK_PA = 0x8142F144
BRK_ORIGINAL = 0xD518C000
EARLY_SHA256 = "baa9674ab5b60ca787be5b7c7d8f8e54b552f0f85f4f09e50535c85395529ebc"
FB_BASE = 0x5E605000
FB_SIZE = 0x17BB000
MARKER = 0x5E9E3000
STRIDE = 2880
ROWS = 32
COLS = 64


def inspect(blob: bytes) -> tuple[int, int, str]:
    check_entry(blob)
    base_va = struct.unpack_from("<Q", blob, 16)[0]
    base_pa = struct.unpack_from("<Q", blob, 56)[0]
    va = base_va + EARLY_PA - base_pa
    segs = [struct.unpack_from("<4Q", blob, 0x2A8 + 32 * i)
            for i in range(10)]
    length = BRK_PA - EARLY_PA + 4
    positions = [fileoff + va - vmaddr for vmaddr, _, fileoff, size in segs
                 if vmaddr <= va and va + length <= vmaddr + size]
    if len(positions) != 1 or positions[0] + length > len(blob):
        raise ValueError("early-XNU path is not entirely file-backed")
    code = blob[positions[0]:positions[0]+length]
    digest = hashlib.sha256(code).hexdigest()
    if digest != EARLY_SHA256:
        raise ValueError("early-XNU machine instructions differ")
    if struct.unpack_from("<I", code, -4)[0] != BRK_ORIGINAL:
        raise ValueError("XNU VBAR instruction changed")
    last = MARKER + (ROWS - 1) * STRIDE + COLS * 4
    if not (FB_BASE <= MARKER < last <= FB_BASE + FB_SIZE):
        raise ValueError("framebuffer marker outside cactus reserved framebuffer")
    if (MARKER - FB_BASE) % STRIDE or COLS * 4 > STRIDE:
        raise ValueError("invalid framebuffer marker row layout")
    return EARLY_PA, BRK_PA, digest


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("payload", type=Path)
    args = parser.parse_args()
    result = inspect(args.payload.read_bytes())
    print("STAGE20-C early XNU preflight PASS",
          f"entry={result[0]:#x} breakpoint={result[1]:#x}",
          f"sha256={result[2]} marker_row={(MARKER-FB_BASE)//STRIDE}")


if __name__ == "__main__":
    main()
