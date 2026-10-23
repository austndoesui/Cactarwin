# Stage23 / build Stage28 — XNU continuation and visible FB probe

**Hardware test: real XNU exception captured on Redmi 6A (2026-10-23).**

Boot image flashed successfully with fastboot (`product: cactus`), then
rebooted with independent COM25 capture. First two runs contained only
44 CRC-valid frames each, through the framebuffer readback marker
`0x2302`; neither reached the `0x2303` XNU handoff. The third run did.

Hardware capture:
`F:\ROM\patching\redmi 6a xnu\usb-captures\stage-cactus-20261023-142123-475\session-003-142252-861.bin`

Strict `--require-stage23`: **STAGE23 GENUINE XNU FAULT CAPTURED; COM25 HELD**.
58 valid CLOG frames (3,248 bytes), zero corruption, sequence gaps,
duplicates or regressions. The shim emitted `0x2303` (native entry),
`0x20a1` (EL1), and a first real synchronous exception:

- `ESR_EL1 = 0x02000000` (EC=0; unknown/unclassified exception).
- `ELR_EL1 = 0xfffffff0072a9e64`, `FAR_EL1 = 0`.
- `SCTLR_EL1 = 0x34d4593d`, `TCR_EL1 = 0x22a5192519`,
  `VBAR_EL1 = 0xfffffff007429000`.
- `TTBR0_EL1 = 0x824c0000`, `TTBR1_EL1 = 0x824c1000`,
  `MAIR_EL1 = 0x0c08044f00bb44ff`.
- `0x220a` class 2 (real exception); `0x220b` vectors restored (1,1,1),
  `0x2308` hold (0,1,1); zero completion statuses. The ELF's MMU
  translation bases changed from Stage22, evidence of additional
  initialization before the exception. This does **not** prove XNU
  reached a scheduler or fully initialized the hardware.

Framebuffer RAM samples passed (`0x2302`=1) in all sessions, but the
user later confirmed **only the MI logo was visible** (no colored bars).
The tested RAM address is not confirmed as the active scanout surface.
Following the test, `fastboot oem reboot-recovery` returned
OKAY; ADB reported `recovery product:omni_cactus model:Redmi_6A`.

Next engineering task: resolve `0xfffffff0072a9e64` against the exact
XNU payload symbol/disassembly and check why EC=0 occurs during the
TTBR transition before attempting additional GIC, timer or display
driver initialization.

Initial exact-payload opcode lookup: ELR VA `0xfffffff0072a9e64`
maps to physical `0x812a9e64`, instruction word **`0xe7ffdeff`**.
Its surrounding words are `0xf9000113` (preceding), `0x9405d782`
(following); this is an *undefined/unallocated AArch64 instruction*
at the fault site, consistent with ESR exception-class 0. Identify
the owning function and whether this is a deliberate unsupported-path
trap before changing the MMU/page tables.

Boot image: `shim/out/cactus/cactus-trace-stage-28-boot.img`

- Size: **45,506,560 bytes**.
- SHA256: **`939ab0864e06fb1c5d7bb6bdc53fdeacdb4dc3892b58c1db2f4cff31ee588e93`**.
- Local WSL cross-build and Android boot-v2 structure: PASS.
- Targeted Stage23 + Stage22 + Stage21D regression/mutation tests: 5 PASS.
- ELF entry links `stage19_probe` and `stage20_el1_transition`; the
  emitted code includes `dc cvac` cache cleaning for framebuffer DMA.

## Why this version is different

The hardware-verified Stage27 installed a deliberate `BRK #0xe24` at
physical `0x81ac0c0c` / VA `0xfffffff007ac0c0c` then returned to
the Linux/TWRP chainloader. Stage28 retains *all* Stage27 layout,
loader and early-C instruction checks and two virtual EL1t/EL1h
synchronous exception stubs, but **does not patch that BRK**. The genuine
`adrp x1` instruction (`0xb0ffacc1`) remains in XNU, allowing continued
native execution beyond the verified point.

If an exception enters the proven HVC #0x21 stub, EL2 records its first
ESR_EL1, ELR_EL1 and FAR_EL1, translation/MMU registers, trap origin,
and restores the vector stubs. Stage28 then polls COM25 indefinitely
(`0x2308` hold marker and periodic `0x1902` heartbeats) without calling
the TWRP chainloader. This is a **captured-fault hold**, not a working
XNU scheduler or permanent USB console. The device may still hang or
reset due to an unrelated watchdog, SError, exception outside the
instrumented EL1 sync slots, or USB reset by XNU. In these cases USB
preflight alone cannot prove post-checkpoint progress. Manual fastboot
and `fastboot oem reboot-recovery` remain available for recovery.

The user reports the **MI logo is visible when COM25 enumerates**.
Therefore the panel and backlight are physically active. Stage28 tests
whether the particular boot_args framebuffer memory actually drives the
scanout. During the 5.5 second COM25 preflight, EL2 writes three
high-contrast **magenta / green / yellow-cyan** bars at x=48..335,
y=80..159 in physical framebuffer `0x5e605000`, 720x1440 32-bit,
stride 2880. Writes stay within a bounded 288x80 rectangle, do not
modify panel registers, and flush pixel lines with `dc cvac`/`dsb sy`.
Events `0x2301/0x2302` report successful RAM readback. **Readback is
not scanout proof**; if MI logo remains unchanged, check LK's actual
front buffer/overlay and DSI display handoff next rather than assuming
the panel is off. A visible corner marker is a separate visual win.

## COM25 events and decoder

- `0x2300`: original PA target/opcode verified; **no synthetic BRK**.
- `0x2301`: marker PA and pixel samples (magenta, green, yellow-cyan).
- `0x2302`: rectangle coordinates/size, 1 = memory readback correct.
- `0x2303`: final pre-EL1 transition (native XNU entry follows).
- `0x20a1`: actual EL1 probe; `0x2206`..`0x220a`: real exception;
  `0x20b3`: HVC; `0x20a3`: return to EL2; `0x220b`: restored vectors.
- `0x2308`: captured-fault hold begins; `0x20ff` and `0x19ff`
  complete diagnostic bookkeeping, followed by 1 Hz `0x1902` beats.
- `0x23ee`: invalid framebuffer video geometry (recover to TWRP).

After a device test, decode a COM25 binary capture:

```powershell
python shim/decode-cactus-usb.py 'CAPTURE.bin' --require-stage23 --show 70
```

`STAGE23 GENUINE XNU FAULT CAPTURED; COM25 HELD` means the synchronous
exception reached the logger and USB hold. `STAGE23 EL1 ENTRY OBSERVED;
XNU OUTCOME UNKNOWN` (exit code 2) means the handoff happened but
no full later synchronous-fault report arrived. Neither indicates
full XNU boot, GIC/timer IRQ/SMP, or visible framebuffer success.

**No fastboot or hardware write was performed as part of this build.**
The Stage27 hardware-success image remains unchanged for rollback.
