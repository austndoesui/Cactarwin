#!/usr/bin/env python3
"""Fail closed if this exact SWEETXNU payload's first branch has changed."""
import argparse
import struct
from pathlib import Path

ENTRY_PA = 0x8142D488
TARGET_PA = 0x8142F0EC
ENTRY_BRANCH = 0x14000719
TARGET_WORD = 0xD510109F


def inspect(blob: bytes) -> tuple[int, int, int, int]:
    if len(blob) < 0x2A8 or blob[:8] != b"SWEETXNU":
        raise ValueError("bad SWEETXNU header")
    version, count = struct.unpack_from("<II", blob, 8)
    virt, entry = struct.unpack_from("<QQ", blob, 16)
    phys = struct.unpack_from("<Q", blob, 0x38)[0]
    if version != 1 or count != 10 or phys != 0x80000000:
        raise ValueError("unexpected image layout")

    segs = [struct.unpack_from("<4Q", blob, 0x2A8 + 32 * i)
            for i in range(count)]

    def word(va: int) -> int:
        matches = [fileoff + va - vmaddr
                   for vmaddr, _, fileoff, size in segs
                   if vmaddr <= va and va + 4 <= vmaddr + size]
        if len(matches) != 1 or matches[0] + 4 > len(blob):
            raise ValueError(f"VA {va:#x} not in one file-backed segment")
        return struct.unpack_from("<I", blob, matches[0])[0]

    entry_pa = phys + entry - virt
    branch = word(entry)
    if branch >> 26 != 0b000101:
        raise ValueError("XNU entry is no longer an A64 B instruction")
    signed = branch & 0x3FFFFFF
    if signed & (1 << 25):
        signed -= 1 << 26
    target = entry + signed * 4
    target_pa = phys + target - virt
    original = word(target)
    if (entry_pa, target_pa, branch, original) != (
            ENTRY_PA, TARGET_PA, ENTRY_BRANCH, TARGET_WORD):
        raise ValueError("XNU instruction or branch target changed")
    return entry_pa, target_pa, branch, original


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("payload", type=Path)
    args = parser.parse_args()
    info = inspect(args.payload.read_bytes())
    print("Stage20-B XNU entry opcode preflight PASS:",
          " ".join(f"{value:#x}" for value in info))


if __name__ == "__main__":
    main()
