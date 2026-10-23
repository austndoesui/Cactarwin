# Stage 20-D — actual XNU VBAR_EL1 and EL1h exception handoff

**2026-10-22: HARDWARE PASS after callee-saved-register correction.**
The previous Stage20-C (stage-22) image passed on the physical cactus
device and returned to TWRP. Stage20-D uses independent stage **23**.

## Reason and target

Stage20-C proved XNU's own framebuffer writes and EL1 execution up to
`0x8142f144` but stopped before `msr vbar_el1,x0`. Stage20-D executes
that instruction, proving the CPU can take a synchronous EL1h exception
through **XNU's VBAR**, while preserving the USB logger and TWRP fallback.
It does not enable XNU's MMU or constitute a full XNU boot.

- XNU entry PA: `0x8142d488`, early code PA `0x8142f0ec`.
- Actual XNU VBAR write at `0x8142f144`, target `0x8142e000`.
- EL1h synchronous vector at `0x8142e200` (VBAR + `0x200`).
- Original vector first words: `14000000 d503201f d503201f d503201f`.
- Reversible 16-byte trampoline: `ldr x16,#8`, `br x16`,
  64-bit runtime handler address. Runtime handler address is obtained by
  PC-relative `adr`, not link-time absolute relocation.
- XNU reads x22=virtBase, x23=physBase, x24=memSize from
  `boot_args` pointer preserved in x20. The authentic
  `adrp x25,...` at `0x8142f154` becomes `brk #0xd20`
  (`0xd421a400`) temporarily. It is restored in EL2 afterward.
- The four vector words are also restored in EL2. Writes are in
  device RAM; no other partitions are touched.

## Required USB evidence

Use `shim/decode-cactus-usb.py --require-stage20d` with the new
stage=23 COM25 capture. Required ordered records are:
`0x20d6` vector armed (VBAR `0x8142e000`, slot `0x8142e200`),
`0x20d0` target BRK armed, `0x20a0` before ERET,
`0x20a1` EL1 reached, `0x20d1` EL1 BRK syndrome
`ESR=0xf2000d20` and ELR `0x8142f154`,
`0x20d2` actual `VBAR_EL1=0x8142e000`,
x20 `0x824bc000`, virtBase `0xfffffff006000000`;
`0x20d3` physical base `0x80000000`,
memSize `0x1e000000` in x24 and boot_args;
`0x20c2` both framebuffer pixels white;
`0x20b3` HVC #0x21, `0x20a3` EL2 return,
`0x20d5` vector/BRK restoration and trap/HVC success `1/1/1`,
then `0x20ff` and `0x19ff` both with zero status.
All ten segments must copy in order, with no CRC errors or frame gaps.

## Hardware results and corrected image

The first image passed the XNU post-VBAR milestones, but then
faulted on return to EL2: `ESR=0x96000000`,
`ELR=0x41ec5c14`, `FAR=0xfffffff006000758`. The XNU early
boot code had overwritten x22 with its virtual base. The shim
EL2 call to `stage20_el1_transition` previously preserved only
x29/x30 and incorrectly expected all other AAPCS64 callee-saved
registers to survive XNU execution. The assembly now saves and
restores x19–x30 as six aligned register pairs.

The corrected image `out/cactus/cactus-trace-stage-23-boot.img` was
compiled and validated with the elevated WSL toolchain:

- Size: **45,504,512 bytes**.
- SHA256: `15acded79146d78971436bee7ac2fe14c3c6f7dcf3cbd4854798466f616f281c`.
- The Android boot v2 structure, payload preflight and AArch64
  prologue/epilogue disassembly passed.
- Fastboot verified `<cactus-fastboot-serial>` as `cactus`; flash and reboot
  commands reported `OKAY`.
- Elevated watcher captured
  `../usb-captures/stage-cactus-20261022-214238-192/session-001-214300-964.bin`:
  **47 CRC-valid frames / 2,632 bytes**, no gaps or corruption.
  `python decode-cactus-usb.py SESSION.bin --require-stage20d`
  returned **STAGE20-D POST-VBAR EL1h VECTOR PASS**.
  Confirmed successful HVC, `0x20a3` EL2 resume,
  `0x20d5 (1,1,1)` full restoration, `0x20ff`/`0x19ff`
  zero-status checkpoints. The device returned to TWRP, verified
  independently by ADB.

Stage20-D proves authentic XNU execution through the VBAR_EL1
handoff with controlled synchronous exception, handler and return.
XNU's later MMU bring-up, paging assumptions and complete boot are
future milestones.

## Historical build/flash preparation

The earlier tool session could run Windows Python tests but
`wsl.exe -d Ubuntu` returned `WSL/E_ACCESSDENIED`. A request to
elevate the WSL build was blocked. This was resolved in the next
session by using the authorized elevated WSL execution path, and
the corrected image above passed device testing.

From WSL Ubuntu in `ios-cactus/shim`:

```bash
python3 test_stage20d_entry.py
python3 test_stage20d_usb.py
./build_trace_probe.sh 23
python3 check_image.py out/cactus/cactus-trace-stage-23-boot.img
sha256sum out/cactus/cactus-trace-stage-23-boot.img
```

Before flashing, inspect the stage-23 ELF disassembly: the vector
trampoline handler must use the runtime address from
`stage20d_vector_handler_address`. Check the output image boot
header and that the stage-22 image remains unchanged.
Only after successful build and verified fastboot serial/product,
start the existing `watch-cactus-usb.ps1` listener, then
flash the **boot partition only** using the previously authorized
fastboot flow. Capture COM25 records, run `--require-stage20d`,
and confirm TWRP recovery by ADB. A black screen alone is not
evidence of failure.
