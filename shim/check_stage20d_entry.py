#!/usr/bin/env python3
"""Pin XNU's actual VBAR installation, post-VBAR instructions and EL1h slot."""
import argparse
import struct
from pathlib import Path

from check_stage20c_entry import inspect as stage20c_inspect

ENTRY = 0x8142D488
VBAR_BASE = 0x8142E000
SYNCH_SLOT = VBAR_BASE + 0x200
TARGET = 0x8142F154
ORIGINAL = (0xF9400696, 0xF9400A97, 0xF9400E98, 0xF0FFEA59)
VECTOR_ORIGINAL = (0x14000000, 0xD503201F, 0xD503201F, 0xD503201F)
BRK_WORD = 0xD421A400  # brk #0xd20
STUB_WORDS = (0x58000050, 0xD61F0200)  # ldr x16,#8; br x16


def read_pa(blob: bytes, pa: int, size: int) -> bytes:
    virtual_base = struct.unpack_from("<Q", blob, 16)[0]
    physical_base = struct.unpack_from("<Q", blob, 56)[0]
    va = virtual_base + pa - physical_base
    segments = (struct.unpack_from("<4Q", blob, 0x2A8 + 32*i)
                for i in range(10))
    offsets = [fileoff + va - vmaddr
               for vmaddr, _, fileoff, file_size in segments
               if vmaddr <= va and va + size <= vmaddr + file_size]
    if len(offsets) != 1 or offsets[0]+size > len(blob):
        raise ValueError(f"PA {pa:#x} not completely file-backed")
    return blob[offsets[0]:offsets[0] + size]


def inspect(blob: bytes):
    stage20c_inspect(blob)
    if VBAR_BASE & 0x7FF or SYNCH_SLOT & 0x7F:
        raise ValueError("VBAR_EL1 or vector slot has invalid alignment")
    after = struct.unpack("<4I", read_pa(blob, 0x8142F148, 16))
    if after != ORIGINAL:
        raise ValueError("XNU post-VBAR instruction sequence changed")
    slot = struct.unpack("<4I", read_pa(blob, SYNCH_SLOT, 16))
    if slot != VECTOR_ORIGINAL:
        raise ValueError("XNU EL1h synchronous vector slot changed")
    return VBAR_BASE, SYNCH_SLOT, TARGET


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("payload", type=Path)
    args = parser.parse_args()
    base, slot, target = inspect(args.payload.read_bytes())
    print(f"STAGE20-D VBAR/VECTOR PREFLIGHT PASS "
          f"vbar={base:#x} EL1h_sync_slot={slot:#x} BRK={target:#x}")


if __name__ == "__main__":
    main()
