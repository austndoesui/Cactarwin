#!/usr/bin/env python3
"""Read-only MMU entry audit of the *relocated*, packed SWEETXNU payload.

Pins instructions actually observed in the cactus Stage20-D entry path.
This is a static, revision-specific audit, NOT proof of an MMU-on boot.
"""
import argparse
import hashlib
import struct
from pathlib import Path

MASK64 = (1 << 64) - 1
FB_BASE, FB_W, FB_H, FB_STRIDE = 0x5E605000, 720, 1440, 2880
FB_ALIAS_VA, FB_ALIAS_PA, FB_ALIAS_SIZE = (
    0xFFFFFFE000200000, 0x5E600000, 0x400000
)
POOL_PA, POOL_BYTES = 0x8117A000, 0x4000
TCR_PA, TCR_VALUE = 0x81178A18, 0x22A5192519
SCTLR_VALUE = 0x3454593D
MAIR_VALUE = 0x0C08044F00BB44FF
BOOTSTRAP_CODE_SHA256 = (
    "f919af47f5d3d9e5b0bbe35356a3018b3b25369cfb8b51c7e140c8591654d8d9"
)

# Every write to the active XNU translation registers, and the two
# branches that select the actual startup path.
PINNED_OPCODES = {
    0x8142F0F8: 0xD2800015,   # mov x21, #0: startup takes cbz path
    0x8142F154: 0xF0FFEA59,   # adrp x25, 0x8117a000
    0x8142F174: 0xD50041BF,   # msr SPSel,#1 (SP_EL1)
    0x8142F18C: 0xD50040BF,   # msr SPSel,#0 (SP_EL0, i.e. EL1t)
    0x8142F1CC: 0x36F80161,   # tbz header flags,#31
    0x8142F1F8: 0xCB170012,   # x18 = first Mach-O PA - physBase
    0x8142F1FC: 0xCB120318,   # remaining RAM size minus x18
    0x8142F220: 0xD355FF05,   # lsr x5,x24,#21; 2 MiB block count
    0x8142F254: 0xF900014C,   # descriptor into TTBR0 table
    0x8142F378: 0xF90000C8,   # framebuffer alias table descriptor
    0x8142F3CC: 0xD5182041,   # msr TCR_EL1,x1
    0x8142F3D8: 0x940012E3,   # bl 0x81433f64 (TTBR0 helper)
    0x8142F3E8: 0xD5182020,   # msr TTBR1_EL1,x0
    0x8142F430: 0xD518A200,   # msr MAIR_EL1,x0
    0x8142F450: 0xB40000B5,   # cbz x21; no override TTBR1
    0x8142F474: 0xD518C000,   # msr VBAR_EL1,x0 (virtual base)
    0x8142F478: 0xF2E00000,   # begin SCTLR immediate
    0x8142F47C: 0xF2C00000,
    0x8142F480: 0xF2A68A80,
    0x8142F484: 0xF28B27A0,
    0x8142F488: 0xD5181000,   # msr SCTLR_EL1,x0; first MMU enable
    0x8142F48C: 0xD5033FDF,   # isb, new translations required
    0x8142F4A4: 0x8B160280,   # x0 = boot_args VA
    0x81433F68: 0xD5182000,   # helper: msr TTBR0_EL1,x0
    0x81429000: 0x14000400,   # post-MMU current EL SP0 sync vector
    0x81429200: 0x140003F6,   # old Stage20-D SPx sync vector equivalent
}


class Payload:
    def __init__(self, data: bytes):
        self.data = data
        if len(data) < 0x2A8 or data[:8] != b"SWEETXNU":
            raise ValueError("invalid SWEETXNU payload")
        version, count = struct.unpack_from("<II", data, 8)
        if version != 1 or count != 10:
            raise ValueError("unexpected payload version or segment count")
        (self.virt, self.entry, self.adt_offset, self.adt_size,
         self.kernel_end, self.phys, self.mem_size) = struct.unpack_from(
             "<7Q", data, 16
         )
        self.segs = [struct.unpack_from("<4Q", data, 0x2A8 + 32*i)
                     for i in range(count)]
        if self.adt_offset + self.adt_size > len(data):
            raise ValueError("device tree outside payload")

    def read(self, pa: int, length: int) -> bytes:
        va = (self.virt + pa - self.phys) & MASK64
        offsets = [off + va - segva for segva, _, off, filesz in self.segs
                   if segva <= va and va + length <= segva + filesz]
        if len(offsets) != 1 or offsets[0] + length > len(self.data):
            raise ValueError(f"not uniquely file-backed: {pa:#x}")
        offset = offsets[0]
        return self.data[offset:offset+length]

    def word(self, pa: int) -> int:
        return struct.unpack("<I", self.read(pa, 4))[0]

    def qword(self, pa: int) -> int:
        return struct.unpack("<Q", self.read(pa, 8))[0]


def audit(data: bytes) -> dict:
    p = Payload(data)
    if (p.virt, p.phys, p.mem_size, p.entry) != (
        0xFFFFFFF006000000, 0x80000000, 0x1E000000,
        0xFFFFFFF00742D488
    ):
        raise ValueError("kernel layout changed from hardware-proven Stage20-D")
    for pa, expected in PINNED_OPCODES.items():
        word = p.word(pa)
        if word != expected:
            raise ValueError(f"startup opcode changed at {pa:#x}: "
                             f"{word:#x} != {expected:#x}")
    if hashlib.sha256(p.read(0x8142F154, 0x8142F490-0x8142F154)
                      ).hexdigest() != BOOTSTRAP_CODE_SHA256:
        raise ValueError("MMU setup instructions changed between checkpoints")
    if p.qword(TCR_PA) != TCR_VALUE:
        raise ValueError("TCR_EL1 constant changed")
    if p.word(0x81004018) & 0x80000000:
        raise ValueError("Mach-O flag changes startup RAM adjustment path")
    if any(p.read(POOL_PA, POOL_BYTES)):
        raise ValueError("bootstrap page-table pool no longer zero-initialized")

    tcr = p.qword(TCR_PA)
    cfg = {
        "T0SZ": tcr & 0x3F, "T1SZ": (tcr >> 16) & 0x3F,
        "TG0": (tcr >> 14) & 3, "TG1": (tcr >> 30) & 3,
        "IPS": (tcr >> 32) & 7, "EPD0": (tcr >> 7) & 1,
        "EPD1": (tcr >> 23) & 1,
    }
    if cfg != {"T0SZ": 25, "T1SZ": 25, "TG0": 0, "TG1": 2,
               "IPS": 2, "EPD0": 0, "EPD1": 0}:
        raise ValueError(f"unexpected translation control {cfg}")
    # Original entry and associated virtual addresses retain 0x1004000
    # displacement from the beginning of kernel-managed RAM.
    first_pa = p.phys + p.segs[0][0] - p.virt
    adjustment = first_pa - p.phys
    remaining = p.mem_size - adjustment
    block_count = remaining >> 21
    map_pa_start = first_pa & ~((1 << 21) - 1)
    map_pa_end = map_pa_start + block_count * (1 << 21)
    memory_end = p.phys + p.mem_size
    stack_pa = 0x820AC000
    stack_va = (stack_pa - p.phys + p.virt) & MASK64
    fb_end = FB_BASE + FB_H * FB_STRIDE
    if not (map_pa_start <= stack_pa < map_pa_end):
        raise ValueError("early XNU stack not covered by bootstrap blocks")
    if not (FB_ALIAS_PA <= FB_BASE < fb_end <=
            FB_ALIAS_PA + FB_ALIAS_SIZE):
        raise ValueError("two framebuffer alias blocks cannot cover full panel")
    for pa in (0x8142F488, 0x81429000, 0x824B8000, 0x824BC000):
        if not map_pa_start <= pa < map_pa_end:
            raise ValueError(f"required early PA not covered: {pa:#x}")
    return {
        "payload_sha256": hashlib.sha256(data).hexdigest(),
        "tcr": tcr, "translation": cfg,
        "mair": MAIR_VALUE, "sctlr": SCTLR_VALUE,
        "table_pool": (POOL_PA, POOL_PA+POOL_BYTES),
        "ttbr0": POOL_PA, "ttbr1": POOL_PA+0x1000,
        "first_kernel_pa": first_pa,
        "ram_window": (p.phys, memory_end),
        "adjusted_ram": (first_pa, remaining),
        "bootstrap_ram_map": (map_pa_start, map_pa_end),
        "unmapped_top_bytes": memory_end - map_pa_end,
        "stack": (stack_pa, stack_va),
        "fb_phys": (FB_BASE, fb_end),
        "fb_alias": (FB_ALIAS_PA, FB_ALIAS_VA, FB_ALIAS_SIZE),
        "mmu_on_pa": 0x8142F488, "mmu_isb_pa": 0x8142F48C,
        "virtual_vbar_write_pa": 0x8142F474,
        "virtual_vbar_value": 0xFFFFFFF007429000,
        "post_mmu_el1t_sync_slot_pa": 0x81429000,
        "pre_mmu_el1t_sync_slot_pa": 0x8142E000,
        "current_sp_at_mmu_enable": "SP_EL0 (EL1t)",
        "post_mmu_entry_pa": 0x8142F490,
        "post_mmu_return_va": 0xFFFFFFF007AC05DC,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("payload", type=Path)
    args = parser.parse_args()
    report = audit(args.payload.read_bytes())
    print("STAGE21-B MMU STATIC AUDIT PASS")
    for key, value in report.items():
        if isinstance(value, int):
            value = f"{value:#x}"
        elif isinstance(value, tuple):
            value = ", ".join(f"{x:#x}" for x in value)
        print(f"{key}: {value}")
    print("NOTE: these are pinned static instructions, not an MMU-on hardware test")


if __name__ == "__main__":
    main()
