#!/usr/bin/env python3
"""Stage26 payload guard: XNU high-VA C prologue and both sync vectors."""
import argparse
import struct
from pathlib import Path

from check_stage21b_mmu import Payload, audit
from check_stage21c2_entry import inspect as inspect_c2

ENTRY = 0x81AC05DC
TARGET = 0x81AC061C
TARGET_VA = 0xFFFFFFF007AC061C
EL1H_VECTOR = 0x81429200
PROLOGUE = (
    0xD10343FF, 0xA9076FFC, 0xA90867FA, 0xA9095FF8,
    0xA90A57F6, 0xA90B4FF4, 0xA90C7BFD, 0x910303FD,
    0xD0000A68, 0xAA0003F3, 0x90FFAE01, 0x91353021,
    0x52800020, 0xD503201F, 0xF9459108, 0xF81A83A8,
)
EL1H_ORIGINAL = (0x140003F6, 0xD503201F, 0xD503201F, 0xD503201F)


def inspect(data):
    inspect_c2(data)
    p = Payload(data)
    mmu = audit(data)
    if tuple(struct.unpack("<16I", p.read(ENTRY, 64))) != PROLOGUE:
        raise ValueError("high VA C prologue changed")
    if p.word(TARGET) != 0xA903FFFF:
        raise ValueError("high VA checkpoint instruction changed")
    if tuple(struct.unpack("<4I", p.read(EL1H_VECTOR, 16))) != EL1H_ORIGINAL:
        raise ValueError("EL1h sync vector changed")
    if mmu["post_mmu_return_va"] != 0xFFFFFFF007AC05DC:
        raise ValueError("post MMU continuation changed")
    if (p.virt + TARGET - p.phys) & ((1 << 64) - 1) != TARGET_VA:
        raise ValueError("checkpoint virtual alias changed")
    lo, hi = mmu["bootstrap_ram_map"]
    if any(not lo <= pa < hi for pa in (ENTRY, TARGET, EL1H_VECTOR)):
        raise ValueError("checkpoint or vector falls outside bootstrap map")
    return TARGET, TARGET_VA


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("payload", type=Path)
    args = parser.parse_args()
    pa, va = inspect(args.payload.read_bytes())
    print(f"STAGE21-D HIGH-VA PREFLIGHT PASS PA={pa:#x} VA={va:#x}")


if __name__ == "__main__":
    main()
