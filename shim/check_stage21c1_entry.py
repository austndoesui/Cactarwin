#!/usr/bin/env python3
"""Fail-closed Stage24 pre-MMU EL1t vector/BRK payload check."""
import argparse
import struct
from pathlib import Path

from check_stage21b_mmu import Payload, audit

TARGET = 0x8142F474
ORIGINAL = 0xD518C000
BRK = 0xD421C420   # brk #0xe21
VECTOR = 0x8142E000
VECTOR_ORIGINAL = (0x14000000, 0xD503201F, 0xD503201F, 0xD503201F)
VECTOR_HVC = (0xD4000422, 0x14000000, 0xD503201F, 0xD503201F)


def inspect(data: bytes):
    report = audit(data)
    p = Payload(data)
    if p.word(TARGET) != ORIGINAL:
        raise ValueError("Stage24 target no longer MSR VBAR_EL1")
    vector = struct.unpack("<4I", p.read(VECTOR, 16))
    if vector != VECTOR_ORIGINAL:
        raise ValueError(f"Stage24 EL1t sync vector changed: {vector!r}")
    if report["pre_mmu_el1t_sync_slot_pa"] != VECTOR:
        raise ValueError("unexpected EL1t vector")
    return TARGET, VECTOR


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("payload", type=Path)
    args = parser.parse_args()
    target, vector = inspect(args.payload.read_bytes())
    print(f"STAGE21-C1 PRE-MMU PREFLIGHT PASS BRK={target:#x} "
          f"EL1t_slot={vector:#x} direct_HVC=0x21")


if __name__ == "__main__":
    main()
