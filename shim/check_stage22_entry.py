#!/usr/bin/env python3
"""Guard the combined Stage22 late C milestone against payload drift."""
import argparse
from pathlib import Path

from check_stage21b_mmu import Payload, audit
from check_stage21d_entry import inspect as inspect_stage21d

TARGET = 0x81AC0C0C
TARGET_VA = 0xFFFFFFF007AC0C0C
ORIGINAL = 0xB0FFACC1

# Branches, relocation join and the final initialization call sequence.
# Paired with runtime opcode checks before any XNU patching.
PINS = {
    0x81AC0624: 0x97E59363,
    0x81AC0628: 0x97E58A6A,
    0x81AC0638: 0x37F805C9,
    0x81AC0668: 0x540029C0,
    0x81AC06EC: 0x1400012D,
    0x81AC0B9C: 0x97E58399,
    0x81AC0BA0: 0x90FFB628,
    0x81AC0BF8: 0x97E5CEE6,
    0x81AC0BFC: 0x97E5CBA9,
    0x81AC0C00: 0x97E5CB90,
    0x81AC0C04: 0x97E5CEDB,
    0x81AC0C08: 0x97E5CEDE,
    TARGET: ORIGINAL,
}


def inspect(data):
    inspect_stage21d(data)
    p = Payload(data)
    report = audit(data)
    for pa, expected in PINS.items():
        got = p.word(pa)
        if got != expected:
            raise ValueError(f"Stage22 opcode {pa:#x}: {got:#x} != {expected:#x}")
    if (p.virt + TARGET - p.phys) & ((1 << 64)-1) != TARGET_VA:
        raise ValueError("Stage22 high VA alias moved")
    lo, hi = report["bootstrap_ram_map"]
    if not lo <= TARGET < hi:
        raise ValueError("Stage22 checkpoint outside early RAM mapping")
    # b.eq at 0x81ac0668 and unconditional branch at 0x81ac06ec
    # both lead to the same C path at 0x81ac0ba0.
    if (0x81AC0668 + (((PINS[0x81AC0668] >> 5) & 0x7FFFF) << 2)
            != 0x81AC0BA0 or
            0x81AC06EC + ((PINS[0x81AC06EC] & 0x3FFFFFF) << 2)
            != 0x81AC0BA0):
        raise ValueError("Stage22 branch join moved")
    return TARGET, TARGET_VA


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("payload", type=Path)
    args = ap.parse_args()
    pa, va = inspect(args.payload.read_bytes())
    print(f"STAGE22 LATE C PREFLIGHT PASS PA={pa:#x} VA={va:#x}")


if __name__ == "__main__":
    main()
