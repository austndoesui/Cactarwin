#!/usr/bin/env python3
"""Pin the actual Stage25 post-MMU instruction and translated EL1t vector."""
import argparse
import struct
from pathlib import Path

from check_stage21b_mmu import Payload, audit

TARGET = 0x8142F490
ORIGINAL = 0xD2A00600  # First instruction after MSR SCTLR_EL1; ISB.
BRK = 0xD421C440       # brk #0xe22
VECTOR = 0x81429000   # physical backing of the virtual EL1t vector
VBAR_VA = 0xFFFFFFF007429000
VECTOR_ORIGINAL = (0x14000400, 0xD503201F, 0xD503201F, 0xD503201F)
VECTOR_HVC = (0xD4000422, 0x14000000, 0xD503201F, 0xD503201F)


def inspect(data: bytes):
    report = audit(data)
    p = Payload(data)
    if p.word(0x8142F488) != 0xD5181000 or p.word(0x8142F48C) != 0xD5033FDF:
        raise ValueError("MMU MSR and ISB sequence changed")
    if p.word(TARGET) != ORIGINAL:
        raise ValueError("first post-ISB instruction changed")
    if struct.unpack("<4I", p.read(VECTOR, 16)) != VECTOR_ORIGINAL:
        raise ValueError("XNU virtual EL1t sync vector changed")
    if report["post_mmu_el1t_sync_slot_pa"] != VECTOR or (
            report["virtual_vbar_value"] != VBAR_VA or
            report["post_mmu_entry_pa"] != TARGET):
        raise ValueError("expected post-MMU vector/target moved")
    first, end = report["bootstrap_ram_map"]
    if not (first <= TARGET < end and first <= VECTOR < end):
        raise ValueError("target or vector missing from physical mappings")
    if (p.virt + VECTOR - p.phys) & ((1 << 64) - 1) != VBAR_VA:
        raise ValueError("XNU vector VA-to-PA alias changed")
    return TARGET, VECTOR, VBAR_VA


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("payload", type=Path)
    args = parser.parse_args()
    target, vector, vbar = inspect(args.payload.read_bytes())
    print(f"STAGE21-C2 POST-MMU PREFLIGHT PASS BRK={target:#x} "
          f"EL1t_vector_PA={vector:#x} VBAR_VA={vbar:#x}")


if __name__ == "__main__":
    main()
