# Stage 28 — real XNU framebuffer mapping and visible scanout

**2026-10-23: Stage33 diagnostic hardware capture completed. XNU still
fails inside `pmap_map_bd()` on an empty L1 page-table descriptor.
The separate display scanout problem is unresolved.**

## Hardware-proven failure after rebuilt XNU

Stage27 rebuilt XNU on GitHub Actions run `39995786681`, using ordered
patches through `0086-cactus-early-io-l1-tables.patch`. The Stage32
full-height boot image copied its segments, entered EL1 and encountered
the original pmap panic. The Stage33 debug image was built locally
with the same new Mach-O and the old strictly guarded pre-panic BRK.
Device `product: cactus`; fastboot flash and reboot both succeeded.

Live COM25 trace:
`../usb-captures/stage-cactus-20261023-235232-019/session-002-235347-971.bin`.
Strict decode with `--require-stage28` succeeded: **78 valid frames**,
zero CRC errors, missing packets or duplicate records, and restored
the original panic branch on return.

- Exact null-PTE VA: **`0xfffffff03b000000`**.
- Initial framebuffer VA: **`0xfffffff03adbb000`**.
- Physical framebuffer interval: `0x5e605000..0x5e9fa000` (end exclusive).
- TTBR1 root: `0x824c1000`.
- Missing L1 slot: **`0x824c1e00`**, descriptor `0x0`.
- The framebuffer mapping needs to extend to approximately
  `0xfffffff03b1b0000`, **0x1b0000 past the missing 1 GiB boundary**.

The old XNU image instead failed at `0xfffffff04a000000`. The
recompiled kernel therefore changed the failing virtual region,
but not the missing-parent-table failure mode. Do not reuse the old
virtual address as a hardcoded new pmap location.

## Source patch candidate

`xnu/patches/0087-cactus-early-io-extent.patch` adds **16 MiB** to the
Cactus-only calculated `va_l1_end` just after its existing 8 MiB
alignment. Patch `0086` then preallocates missing L1 parent pages
and the original loop allocates L3 pages across the larger I/O window.
The additional bytes exceed the measured framebuffer overrun, while
remaining a small bounded virtual space reservation; it is *not*
a change to framebuffer resolution, the physical RAM map, or the
actual display controller's memory address.

The patch applied to local XNU using `git apply --check` and `git apply`.
It requires a **new** macOS/XNU build and hardware test before the
page-table fault can be called fixed. The compiled Stage27 Mach-O does
not contain patch 0087.

## Scanout is a separate problem

LK leaves the MI logo/backlight active while COM25 runs. The Stage23
shim proved cache-cleaned RGB test bars can be written and read back
at physical `0x5e605000`. The user did **not** see these bars.
Strings extracted from the local `lk.img` show separate
`logo_db_addr_pa`, `mt_get_tempfb_addr`, `videolfb-fb_base`, and
`[LK_DDP/OVL]` hardware layer address logging. These establish that
LK has a display/overlay path; they **do not identify the live
scanned-out physical buffer or prove that `0x5e605000` is active**.

After the virtual mapping is verified: inspect reliable LK handoff
metadata and verified Cactus display-controller source to determine
the OVL/RDMA active address, pixel format, stride, visible plane,
and cache requirements. Make a tiny, bounded marker on the confirmed
active plane and report its physical address over COM25. Avoid
speculative writes to display MMIO registers or framebuffer RAM.
Only when this marker is actually visible should an early text renderer
mirror shim/XNU diagnostic lines to screen. COM25 remains the primary
truth while native XNU startup is incomplete.

## Next hardware gates

1. Build the patched XNU image with `0086` **and** `0087`, machine
   configuration `CACTUS`; inspect the resulting ARM64 Mach-O and
   repair shim code-address guards against the new instructions.
2. Build a normal native first-fault shim with the full 720x1440
   boot_args, and keep USB register snapshots; compare new ELR/ESR to
   the Stage27 and Stage33 captures.
3. Only claim the I/O mapping repair when COM25 shows XNU executing
   beyond the old panic. If it still fails, capture the exact new
   missing descriptor before adjusting source again.
4. Independently prove a **visible** framebuffer marker from a verified
   live scanout buffer, then add a minimal on-screen text console.
