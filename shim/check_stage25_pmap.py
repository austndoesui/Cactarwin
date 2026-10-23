#!/usr/bin/env python3
"""Pin the exact XNU null-PTE panic join before installing Stage25 BRK."""
import argparse
from pathlib import Path

from check_stage21b_mmu import Payload
from check_stage22_entry import inspect as stage22_inspect

TARGET_PA = 0x81405218
TARGET_VA = 0xFFFFFFF007405218
ORIGINAL = 0x941AE25E
PANIC_CALLEE = 0x81ABDB90
PINS = {
    0x81404F90: 0xD102C3FF,  # pmap_map_bd stack allocation
    0x81404FB0: 0xAA0203F5,  # x21 = physical end
    0x81404FB4: 0xAA0003F4,  # x20 = first VA
    0x81404FB8: 0xAA0103F6,  # x22 = physical start
    0x81404FBC: 0xAA0003F3,  # x19 = current VA
    0x8140501C: 0x54000FE9,  # null/invalid page-table branch
    0x81405064: 0xB4000DA0,  # null L3-table branch
    TARGET_PA: ORIGINAL,      # bl _pmap_map_bd.cold.1
    PANIC_CALLEE: 0xD10083FF,
}


def inspect(data: bytes):
    stage22_inspect(data)
    p = Payload(data)
    for pa, expected in PINS.items():
        actual = p.word(pa)
        if actual != expected:
            raise ValueError(f"pmap opcode drift at {pa:#x}: {actual:#x} != {expected:#x}")
    if (p.virt + TARGET_PA - p.phys) & ((1 << 64)-1) != TARGET_VA:
        raise ValueError("Stage25 high VA target drifted")
    offset = ORIGINAL & 0x03FFFFFF
    if offset & (1 << 25):
        offset -= 1 << 26
    if TARGET_PA + offset*4 != PANIC_CALLEE:
        raise ValueError("pmap null-PTE branch no longer calls the pinned panic")
    return TARGET_PA, TARGET_VA


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("payload", type=Path)
    args = parser.parse_args()
    print("STAGE25 PMAP PANIC-JOIN GUARD PASS", inspect(args.payload.read_bytes()))
