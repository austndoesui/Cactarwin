# Stage24 / build Stage29 — XNU panic provenance and frame walk

**Hardware Stage24 panic provenance PASS (2026-10-23). Full XNU boot
still blocked.**

Stage29 first run with basic register snapshot was flashed and traced:
`F:\ROM\patching\redmi 6a xnu\usb-captures\stage-cactus-20261023-145638-471\session-001-145806-881.bin`.
68 CRC-valid frames, strict `--require-stage24` PASS. The debugger's
registers identify **`_pmap_map_bd.cold.1+0x24`** as panic caller.
`x1` points at ASCII `panic`, `x2` at `pmap_map_bd @%s:%d`.

A second Stage29 image added bounded `__BOOTDATA` physical translation
for the original x29/intstack chain and passed local verification. It
was flashed successfully; capture:
`F:\ROM\patching\redmi 6a xnu\usb-captures\stage-cactus-20261023-150509-022\session-001-150557-967.bin`.
70 valid frames, no CRC errors, gaps or duplicates. Strict
`--require-stage24`: **STAGE24 GENUINE XNU FAULT CAPTURED; COM25 HELD**.
`0x2410` confirms x29=`0xfffffff0080a3cd0`, but the saved previous FP
and saved LR there both read zero; further call-stack unwinding was
not available. `0x241e` proves the bounded direct __BOOTDATA lookup was
used. The register evidence independently identifies the same
`_pmap_map_bd.cold.1+0x24` path as the first capture.

**Underlying source failure:** In local XNU
`/home/lapdq/xnu-cactus/osfmk/arm/pmap/pmap.c:1883-1886`,
`pmap_map_bd()` calls `pmap_pte(kernel_pmap,vaddr)` and panics
`"pmap_map_bd"` if the returned pointer is `PT_ENTRY_NULL`.
In `/home/lapdq/xnu-cactus/osfmk/arm/io_map.c:95-114`, early
`io_map()` reserves its VA from `virtual_space_start`, then calls
`pmap_map_bd(start,phys_addr,phys_addr+alloc_size,prot)`.
**The missing PTE's exact virtual address and the device PA are not yet
known.** The next patch must instrument the early I/O mapping request,
then create or reserve the necessary early page-table entries; simply
skipping XNU's debugger opcode `0xe7ffdeff` is unsafe.

The COM25 fault handler intentionally holds. Following the second
capture the device did **not** enumerate in ADB or fastboot during the
recovery poll. Manual fastboot entry is necessary, then
`fastboot -s <cactus-fastboot-serial> oem reboot-recovery` to reach TWRP.

Image `shim/out/cactus/cactus-trace-stage-29-boot.img`

Final flashed size **45,506,560 bytes**, SHA256
`6ee308d9407a159200e2dad3cc826fbfc3c56f8843b08b882176df70bc609530`.
Initial Stage29 image SHA256
`54eea2e7abf3917047e21662ca7ef33e08ddaac73d7c6131129054bc2a17bf9e`.

Stage23/build28 demonstrated a *genuine* EL1 sync fault at
`0xfffffff0072a9e64`: ESR_EL1=`0x02000000`, opcode=`0xe7ffdeff`.
Static analysis of **the exact loaded XNU Mach-O `LC_SYMTAB`** now
identifies this location as `_DebuggerTrapWithState +0x84`. The
instruction is explicitly undefined and lies in the kernel's debugger
entry after it saves its state. The first problem is earlier: XNU had
already entered debugger/panic handling. Replacing the instruction
with a NOP would discard the evidence and may corrupt panic state.

Stage29 keeps the verified Stage28 loader, virtual EL1 sync HVC hooks,
native XNU entry, and COM25 fault hold. Before the HVC handler touches
the register state, assembly captures x0..x8, x19..x30, SP_EL0, and
SP_EL1 in an EL2-addressable physical buffer. The vector itself uses
x16 as a branch scratch register, and x9 is overwritten to address
the buffer; those two registers are not claimed as original XNU data.
No virtual pointers are dereferenced by the basic register capture.

USB event `0x2400` holds x29, x30 (link register), SP_EL1;
`0x2401`..`0x2403` hold x0..x8; `0x2404`..`0x2407` hold
x19..x28, SP_EL0 and stage ID 29. XNU frame pointer x29 can contain
the caller's saved x29 and link address. A strictly bounded TTBR1
4KB/39-bit page-table walker only reads memory *proven to be inside*
the accepted 480MiB cactus RAM range. If table geometry, descriptors
or stack VAs fail validation, it gives up cleanly. Optional `0x2410`..
`0x2413` records frame-pointer, previous frame pointer and saved LR;
`0x241f` logs TTBR1 and frame count (0..4). The regular EL1 exception,
MMU state, HVC and restoration events still follow.

The standalone address tool `inspect_stage24_fault.py` reads kernel
symbols **from the embedded Mach-O, without any external debug file**,
and can symbolicate Stage29 register and caller traces automatically:

```powershell
python shim/decode-cactus-usb.py 'CAPTURE.bin' --require-stage24 --show 90
python shim/inspect_stage24_fault.py shim/out/cactus/linux-chainload/stage18-payload.bin --capture 'CAPTURE.bin'
```

Build evidence: WSL `./build_trace_probe.sh 29`, boot-v2
structural verifier and native ELF symbols/entry passed; targeted
Stage24/23/22/21D decoder tests (including dropped/reordered register
events) pass. Stage29 has now been flashed and hardware-tested twice.
The existing Stage28 image remains available for rollback.

The user saw only the MI logo during Stage28. Its framebuffer RAM test
passed but the scanout was not visibly changed. Visible framebuffer
bring-up is still unresolved; this Stage24 image prioritizes tracing
the first startup panic so the subsequent fix can be based on evidence.

**A successful Stage29 trace identifies the root panic caller. It does
not, by itself, mean XNU boot or display services are working.**
